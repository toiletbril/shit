#include "Arena.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

static fn sparse_array_key(StringView name, usize index,
                           Allocator allocator) throws -> String
{
  let key = String{allocator, name};
  key.push('\x01');
  key.append(utils::uint_to_text(index).view());
  return key;
}

/* One sparse element, its index and its value, used to merge the sparse map
   into the dense run when an array is enumerated. */
struct sparse_array_entry
{
  usize index;
  String value;
};

/* Every sparsely-held element of an array, sorted by ascending index. The
   sparse indices always sit beyond the dense run, so appending these after the
   dense elements yields the whole array in index order. */
static fn collect_sparse_array_entries(const StringMap<String> &sparse,
                                       StringView name,
                                       Allocator allocator) throws
    -> ArrayList<sparse_array_entry>
{
  let out = ArrayList<sparse_array_entry>{allocator};
  let const prefix = sparse_array_key(name, 0, allocator);
  /* The prefix is name plus the separator byte, so the leading "0" of the key
     for index zero is dropped to leave just the name and separator. */
  let const name_prefix = prefix.view().substring_of_length(0, name.length + 1);
  sparse.for_each([&](StringView key, const String &value) throws {
    if (key.length <= name_prefix.length ||
        key.substring_of_length(0, name_prefix.length) != name_prefix)
    {
      return;
    }
    let const index_text = key.substring(name_prefix.length);
    if (let const parsed = utils::parse_decimal_integer(index_text);
        !parsed.is_error() && parsed.value() >= 0)
    {
      out.push(sparse_array_entry{
          static_cast<usize>(parsed.value()), String{allocator, value.view()}
      });
    }
  });
  /* An insertion sort keeps it simple, since a sparse array holds few far
     elements. */
  for (usize i = 1; i < out.count(); i++) {
    let key = steal(out[i]);
    usize j = i;
    while (j > 0 && out[j - 1].index > key.index) {
      out[j] = steal(out[j - 1]);
      j--;
    }
    out[j] = steal(key);
  }
  return out;
}

fn EvalContext::clear_sparse_array(StringView name) throws -> void
{
  /* Only the indices are collected, no value is copied and the list is not
     sorted, since the entries are erased by key regardless of order. The erase
     runs after the scan so the map is not mutated while it is walked. */
  let indices = ArrayList<usize>{scratch_allocator()};
  let const prefix = sparse_array_key(name, 0, scratch_allocator());
  let const name_prefix = prefix.view().substring_of_length(0, name.length + 1);
  m_sparse_array_values.for_each(
      [&](StringView key, const String &value) throws {
        unused(value);
        if (key.length <= name_prefix.length ||
            key.substring_of_length(0, name_prefix.length) != name_prefix)
        {
          return;
        }
        if (let const parsed =
                utils::parse_decimal_integer(key.substring(name_prefix.length));
            !parsed.is_error() && parsed.value() >= 0)
        {
          indices.push(static_cast<usize>(parsed.value()));
        }
      });

  for (const usize index : indices)
    m_sparse_array_values.erase(
        sparse_array_key(name, index, scratch_allocator()).view());
}

/* Whether an array-literal element is the explicit [index]=value form, and if
   so its subscript text and its value. A leading '[' with a later "]=" marks
   it, every other element is a positional value. */
static fn parse_explicit_array_index(StringView element,
                                     StringView &subscript_out,
                                     StringView &value_out) wontthrow -> bool
{
  if (element.length < 3 || element[0] != '[') {
    return false;
  }
  for (usize i = 1; i + 1 < element.length; i++) {
    if (element[i] == ']' && element[i + 1] == '=') {
      subscript_out = element.substring_of_length(1, i - 1);
      value_out = element.substring(i + 2);
      return true;
    }
  }
  return false;
}

fn EvalContext::assign_indexed_array_elements(StringView name,
                                              ArrayList<String> elements,
                                              bool is_append) throws -> void
{
  /* POSIX mode has no arrays, but a sourced profile that carries a bash array
     literal should keep sourcing, so the assignment stands in as an empty
     scalar rather than a syntax error, the shim the parser used to apply at
     parse time for every non-bash mood. */
  if (is_posix_mode()) [[unlikely]] {
    LOG(Debug,
        "posix mode stores the array literal for '%.*s' as an empty scalar",
        static_cast<int>(name.length), name.data);
    set_shell_variable(name, "");
    return;
  }

  usize running_index = 0;
  if (is_append) {
    /* An append continues after the highest set index, the dense end or the
       last sparse element, whichever is larger. */
    if (let const *array = lookup_indexed_array(name))
      running_index = array->count();
    let const sparse = collect_sparse_array_entries(m_sparse_array_values, name,
                                                    scratch_allocator());
    if (!sparse.is_empty()) {
      let const next_after_sparse = sparse[sparse.count() - 1].index + 1;
      if (next_after_sparse > running_index) running_index = next_after_sparse;
    }
  } else {
    /* A plain assignment replaces the array, clearing the dense run, the sparse
       elements, and any scalar of the same name. */
    set_indexed_array(name, ArrayList<String>{heap_allocator()});
  }

  for (const String &element : elements) {
    StringView subscript;
    StringView value;
    usize index = running_index;
    if (parse_explicit_array_index(element.view(), subscript, value)) {
      index = static_cast<usize>(evaluate_arithmetic(subscript));
      set_array_element(name, index, value);
    } else {
      set_array_element(name, index, element.view());
    }
    running_index = index + 1;
  }
}

fn EvalContext::set_array_element(StringView name, usize index,
                                  StringView value) throws -> void
{
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};

  /* The dense run holds the contiguous prefix from index zero, and any element
     past its end lives in the sparse map keyed by index, the way bash keeps an
     indexed array without padding a gap. An empty dense array still marks the
     name as indexed so a read routes here. A first write promotes an existing
     scalar to element zero. */
  ArrayList<String> *dense = m_indexed_arrays.find(name);
  if (dense == nullptr) {
    let elements = ArrayList<String>{heap_allocator()};
    if (let const *scalar = m_shell_variables.find(name))
      elements.push(String{heap_allocator(), scalar->view()});
    set_indexed_array(name, steal(elements));
    dense = m_indexed_arrays.find(name);
  }
  m_shell_variables.erase(name);
  ASSERT(dense != nullptr);

  const usize dense_count = dense->count();
  if (index < dense_count) {
    (*dense)[index] = String{heap_allocator(), value};
    return;
  }
  if (index == dense_count) {
    /* The write extends the contiguous run, which may make an earlier sparse
       element contiguous too, so any element now at the run's end migrates from
       the sparse map into the dense run. */
    dense->push(String{heap_allocator(), value});
    for (;;) {
      let const key =
          sparse_array_key(name, dense->count(), scratch_allocator());
      let const *migrated = m_sparse_array_values.find(key.view());
      if (migrated == nullptr) break;
      dense->push(String{heap_allocator(), migrated->view()});
      m_sparse_array_values.erase(key.view());
    }
    return;
  }
  /* A write past the run's end leaves a gap, so it is held sparsely. */
  LOG(All, "holding element %zu of '%.*s' sparsely past the dense run of %zu",
      index, static_cast<int>(name.length), name.data, dense_count);
  m_sparse_array_values.set(
      sparse_array_key(name, index, scratch_allocator()).view(), value);
}

/* The flat-map key for an associative element, the array name and the element
   key joined by a byte that does not occur in a name. The map copies a key it
   stores, so the callers build this on the per-command scratch arena. */
static fn associative_composite_key(StringView name, StringView key,
                                    Allocator allocator) throws -> String
{
  let composite = String{allocator, name};
  composite.push('\x01');
  composite.append(key);
  return composite;
}

fn EvalContext::assign_array_element(StringView name, StringView subscript,
                                     StringView value, bool is_append) throws
    -> void
{
  LOG(All, "assigning the array element '%.*s[%.*s]'",
      static_cast<int>(name.length), name.data,
      static_cast<int>(subscript.length), subscript.data);
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};

  /* An integer-marked name evaluates the element text as arithmetic and an
     append adds the prior element's evaluation, the same treatment
     set_shell_variable gives a scalar. The joined text lives on the scratch
     arena and the stores below copy the decimal result. */
  char integer_result[24];
  auto do_integer_element_value = [&](Maybe<String> existing)
                                      throws -> StringView {
    let joined = String{scratch_allocator()};
    if (is_append) {
      if (existing.has_value()) joined.append(existing->view());
      append_integer_expression(joined, value);
    } else {
      joined.append(value);
    }
    return utils::int_to_text_into(
        joined.is_empty() ? 0 : evaluate_arithmetic(joined.view()),
        integer_result, sizeof(integer_result));
  };

  if (is_associative_array(name)) {
    const String key = expand_modifier_word(subscript);
    if (is_integer_variable(name)) [[unlikely]] {
      set_associative_element(
          name, key.view(),
          do_integer_element_value(
              lookup_associative_element(name, key.view())));
      return;
    }
    if (is_append) {
      let combined = String{
          lookup_associative_element(name, key.view()).value_or(String{})};
      combined += value;
      set_associative_element(name, key.view(), combined.view());
    } else {
      set_associative_element(name, key.view(), value);
    }
    return;
  }

  i64 index = evaluate_arithmetic(subscript);
  if (index < 0) {
    if (let const *array = lookup_indexed_array(name))
      index += static_cast<i64>(array->count());
  }
  if (index < 0)
    throw Error{"Unable to index '" + name +
                "' because the array subscript is invalid"};

  if (is_integer_variable(name)) [[unlikely]] {
    let existing = Maybe<String>{};
    if (is_append)
      if (let const *array = lookup_indexed_array(name))
        if (static_cast<usize>(index) < array->count())
          existing = String{(*array)[static_cast<usize>(index)].view()};
    set_array_element(name, static_cast<usize>(index),
                      do_integer_element_value(steal(existing)));
    return;
  }

  /* The element text is transient, copied by set_array_element, so it lives
     on the per-command scratch arena. */
  let element = String{scratch_allocator(), value};
  if (is_append) {
    let combined = String{scratch_allocator()};
    if (let const *array = lookup_indexed_array(name))
      if (static_cast<usize>(index) < array->count())
        combined = String{(*array)[static_cast<usize>(index)].view()};
    combined += value;
    element = steal(combined);
  }
  set_array_element(name, static_cast<usize>(index), element.view());
}

fn EvalContext::declare_associative_array(StringView name) throws -> void
{
  LOG(Debug, "declaring '%.*s' as an associative array",
      static_cast<int>(name.length), name.data);
  m_associative_names.add(name);
  m_shell_variables.erase(name);
}

fn EvalContext::set_associative_element(StringView name, StringView key,
                                        StringView value) throws -> void
{
  /* Registering the name and clearing a same-named scalar matters only the
     first time, since the name stays associative until an unset removes it. The
     hot comp[$m]=1 loop sets thousands of elements under one already-registered
     name, so the repeated set probes neither the name set nor the variable
     store. */
  if (!is_associative_array(name)) {
    m_associative_names.add(name);
    m_shell_variables.erase(name);
  }
  m_associative_values.set(
      associative_composite_key(name, key, scratch_allocator()).view(), value);
}

fn EvalContext::lookup_associative_element(StringView name,
                                           StringView key) const throws
    -> Maybe<String>
{
  if (let const *value = m_associative_values.find(
          associative_composite_key(name, key, scratch_allocator()).view()))
    return *value;
  return None;
}

fn EvalContext::associative_keys(StringView name) const throws
    -> ArrayList<String>
{
  let keys = ArrayList<String>{heap_allocator()};
  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  m_associative_values.for_each([&](StringView composite, const String &value) {
    unused(value);
    if (composite.length >= prefix.count() &&
        composite.substring_of_length(0, prefix.count()) == prefix.view())
    {
      keys.push_managed(composite.substring(prefix.count()));
    }
  });
  return keys;
}

fn EvalContext::associative_values(StringView name) const throws
    -> ArrayList<String>
{
  let values = ArrayList<String>{heap_allocator()};
  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  m_associative_values.for_each([&](StringView composite, const String &value) {
    if (composite.length >= prefix.count() &&
        composite.substring_of_length(0, prefix.count()) == prefix.view())
    {
      values.push_managed(value.view());
    }
  });
  return values;
}

fn EvalContext::clear_associative_array(StringView name) throws -> void
{
  if (!is_associative_array(name)) return;
  /* The composite keys are collected before erasing, since removing entries
     while iterating the value map would be unsafe. */
  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  let to_erase = ArrayList<String>{heap_allocator()};
  m_associative_values.for_each([&](StringView composite, const String &) {
    if (composite.length >= prefix.count() &&
        composite.substring_of_length(0, prefix.count()) == prefix.view())
    {
      to_erase.push_managed(composite);
    }
  });
  for (const String &composite : to_erase)
    m_associative_values.erase(composite.view());
  m_associative_names.remove(name);
}

fn EvalContext::unset_array_element(StringView name,
                                    StringView subscript) throws -> void
{
  LOG(All, "unsetting the array element '%.*s[%.*s]'",
      static_cast<int>(name.length), name.data,
      static_cast<int>(subscript.length), subscript.data);
  if (is_readonly(name))
    throw Error{"Unable to unset '" + name + "' because it is read only"};

  if (is_associative_array(name)) {
    m_associative_values.erase(
        associative_composite_key(name, subscript, scratch_allocator()).view());
    return;
  }

  if (ArrayList<String> *array = m_indexed_arrays.find(name)) {
    const i64 index = evaluate_arithmetic(subscript);
    const i64 array_count = static_cast<i64>(array->count());
    const i64 resolved =
        index < 0 ? index + array_negative_index_base(name) : index;
    if (resolved < 0) return;
    /* An element inside the dense run leaves a hole at its index the way bash
       does, rather than renumbering the tail. The elements after it move to the
       sparse store under their original indices and the dense run is dropped
       from the removed index on. An element past the dense run already lives in
       the sparse store and is erased by its key. Erasing an absent key is a
       no-op, matching bash unsetting a missing element silently. */
    if (resolved < array_count) {
      for (usize i = static_cast<usize>(resolved) + 1;
           i < static_cast<usize>(array_count); i++)
        m_sparse_array_values.set(
            sparse_array_key(name, i, scratch_allocator()).view(),
            (*array)[i].view());
      while (array->count() > static_cast<usize>(resolved))
        array->remove(array->count() - 1);
    } else {
      m_sparse_array_values.erase(sparse_array_key(name,
                                                   static_cast<usize>(resolved),
                                                   scratch_allocator())
                                      .view());
    }
  }
}

fn EvalContext::declare_local(StringView name) throws -> void
{
  if (m_local_scopes.is_empty()) return;
  ASSERT(!m_local_scopes.is_empty());
  /* One binding per scope, the bash rule. A second local declaration of the
     same name in the same scope keeps the first declaration's saved caller
     state, so the scope pop restores the true pre-call value and the unset
     peel finds exactly one entry to consume. */
  if (is_local_in_current_scope(name)) return;
  LOG(All, "declaring '%.*s' local in scope depth %zu",
      static_cast<int>(name.length), name.data, m_local_scopes.count());

  /* The indexed array the name held is saved alongside the scalar value, so a
     local array restores the caller's array on return. A copy is taken since
     the body may overwrite the stored array in place. The lookup is skipped
     when no array exists at all, so a scalar local in an array-free script pays
     nothing on the function-call path. */
  let previous_array = Maybe<ArrayList<String>>{};
  if (m_indexed_arrays.count() != 0)
    if (let const *array = lookup_indexed_array(name); array != nullptr) {
      let copy = ArrayList<String>{heap_allocator()};
      copy.reserve(array->count());
      for (const String &element : *array)
        copy.push_managed(element.view());
      previous_array = steal(copy);
    }

  /* The associative array the name held is saved the same way, as parallel key
     and value lists, so a local -A restores the caller's map on return. */
  let const previous_was_associative = is_associative_array(name);
  let previous_keys = ArrayList<String>{heap_allocator()};
  let previous_values = ArrayList<String>{heap_allocator()};
  if (previous_was_associative) {
    previous_keys = associative_keys(name);
    previous_values = associative_values(name);
  }

  /* The sparse elements the name held are saved so a local that shadows a
     caller's sparse array does not wipe those gap indices on return. The scan
     is skipped when no sparse element exists at all. */
  let previous_sparse_indices = ArrayList<usize>{heap_allocator()};
  let previous_sparse_values = ArrayList<String>{heap_allocator()};
  if (m_sparse_array_values.count() != 0)
    for (const sparse_array_entry &entry : collect_sparse_array_entries(
             m_sparse_array_values, name, heap_allocator()))
    {
      previous_sparse_indices.push(entry.index);
      previous_sparse_values.push(String{heap_allocator(), entry.value.view()});
    }

  /* A local starts with no attributes the way bash localizes a name fresh, so
     the caller's integer mark is dropped here and the saved flag puts it back
     when the scope ends. */
  let const previous_was_integer = is_integer_variable(name);
  if (previous_was_integer) unmark_integer(name);

  /* The read-only mark is dropped the same way, so a local shadowing a
     read-only caller name starts writable, and a local -r that marks this scope
     does not leak past it to reject a later reassignment or a second call to
     the same function. */
  let const previous_was_readonly = is_readonly(name);
  if (previous_was_readonly) unmark_readonly(name);

  m_local_scopes.back().push(local_binding{
      String{name}, get_variable_value(name), steal(previous_array),
      previous_was_associative, steal(previous_keys), steal(previous_values),
      steal(previous_sparse_indices), steal(previous_sparse_values),
      previous_was_integer, previous_was_readonly});

  /* The live array forms are cleared so a local array starts empty rather than
     showing the caller's elements, and the saved caller state comes back on the
     scope pop. The scalar value is left in place, so a value-less local of a
     scalar name keeps the caller's value the way this shell already does. */
  m_indexed_arrays.erase(name);
  clear_sparse_array(name);
  clear_associative_array(name);
}

hot fn EvalContext::expand_variable(StringView name) const throws -> String
{
  return get_variable_value(name).value_or(String{});
}

fn EvalContext::array_negative_index_base(StringView name) const throws -> i64
{
  i64 base = 0;
  if (let const *array = m_indexed_arrays.find(name))
    base = static_cast<i64>(array->count());

  /* The highest sparse index is read straight off the shared map, no value is
     copied and no list is built, since only the maximum is wanted. */
  let const prefix = sparse_array_key(name, 0, scratch_allocator());
  let const name_prefix = prefix.view().substring_of_length(0, name.length + 1);
  m_sparse_array_values.for_each(
      [&](StringView key, const String &value) throws {
        unused(value);
        if (key.length <= name_prefix.length ||
            key.substring_of_length(0, name_prefix.length) != name_prefix)
        {
          return;
        }
        if (let const parsed =
                utils::parse_decimal_integer(key.substring(name_prefix.length));
            !parsed.is_error() && parsed.value() >= 0)
        {
          let const past_index = static_cast<i64>(parsed.value()) + 1;
          if (past_index > base) base = past_index;
        }
      });

  return base;
}

fn EvalContext::apply_array_subscript(StringView name,
                                      StringView subscript) throws -> String
{
  /* FUNCNAME reads the call-name stack, index zero the innermost frame the
     way bash exposes it, @ and * the whole stack outward. */
  if (name == "FUNCNAME" && bash_dynamic_variables_enabled()) [[unlikely]] {
    let const depth = funcname_frame_count();
    if (subscript == "@" || subscript == "*") {
      /* The * form joins with the first IFS byte the way "${a[*]}" does in
         bash, and an empty IFS joins with nothing. The @ form keeps the
         space. */
      let separator = ' ';
      let has_separator = true;
      if (subscript == "*") {
        has_separator = !m_field_separators.is_empty();
        if (has_separator) separator = m_field_separators.first_character();
      }
      let out = String{scratch_allocator()};
      for (usize i = 0; i < depth; i++) {
        if (i > 0 && has_separator) {
          out.push(separator);
        }
        out.append(funcname_frame_at(i));
      }
      return out;
    }
    let const index = evaluate_arithmetic(subscript);
    if (index >= 0 && static_cast<usize>(index) < depth) {
      return String{scratch_allocator(),
                    funcname_frame_at(static_cast<usize>(index))};
    }
    return String{scratch_allocator()};
  }

  /* An associative array reads by a string key, with @ and * naming every value
     joined by a space. The values come back in the store's order, which need
     not match bash for more than one key. */
  if (is_associative_array(name)) {
    if (subscript == "@" || subscript == "*") {
      /* The * form joins with the first IFS byte the way "${a[*]}" does in
         bash, and an empty IFS joins with nothing. The @ form keeps the
         space. */
      let separator = ' ';
      let has_separator = true;
      if (subscript == "*") {
        has_separator = !m_field_separators.is_empty();
        if (has_separator) separator = m_field_separators.first_character();
      }
      let out = String{scratch_allocator()};
      let const values = associative_values(name);
      for (usize i = 0; i < values.count(); i++) {
        if (i > 0 && has_separator) {
          out.push(separator);
        }
        out.append(values[i].view());
      }
      return out;
    }
    const String key = expand_modifier_word(subscript);
    return String{
        heap_allocator(),
        lookup_associative_element(name, key.view()).value_or(String{}).view()};
  }

  const ArrayList<String> *array = lookup_indexed_array(name);

  /* @ and * name the whole array, joined with a space. The single-string return
     loses the per-element split of a quoted "${a[@]}", which is the same
     limitation the positional "$@" has here. */
  if (subscript == "@" || subscript == "*") {
    if (array == nullptr) return get_variable_value(name).value_or(String{});
    /* The * form joins with the first IFS byte the way "${a[*]}" does in
       bash, and an empty IFS joins with nothing. The @ form keeps the
       space. */
    let separator = ' ';
    let has_separator = true;
    if (subscript == "*") {
      has_separator = !m_field_separators.is_empty();
      if (has_separator) separator = m_field_separators.first_character();
    }
    let out = String{scratch_allocator()};
    for (usize i = 0; i < array->count(); i++) {
      if (i > 0 && has_separator) out.push(separator);
      out.append((*array)[i].view());
    }
    return out;
  }

  /* An arithmetic subscript selects one element, with a negative index counting
     from the end the way bash does. */
  i64 index = evaluate_arithmetic(subscript);
  if (array == nullptr) {
    /* A scalar reads as a one-element array, so ${name[0]} is the value and any
       other index is empty. */
    if (index == 0) return get_variable_value(name).value_or(String{});
    return String{scratch_allocator()};
  }
  const i64 array_count = static_cast<i64>(array->count());
  if (index < 0) index += array_negative_index_base(name);
  if (index < 0 || index >= array_count) {
    /* A subscript past the dense end may name a sparsely-held far element. */
    if (index >= 0) {
      let probe = String{scratch_allocator(), name};
      probe.push('\x01');
      probe.append(utils::uint_to_text(static_cast<usize>(index)).view());
      if (let const *sparse = m_sparse_array_values.find(probe.view()))
        return String{scratch_allocator(), sparse->view()};
    }
    return String{scratch_allocator()};
  }
  return String{scratch_allocator(),
                (*array)[static_cast<usize>(index)].view()};
}

fn EvalContext::collect_array_elements(StringView name) const throws
    -> ArrayList<String>
{
  /* FUNCNAME enumerates the call-name stack, innermost first the way bash
     orders it, so "${FUNCNAME[@]}" yields one frame per field. */
  if (name == "FUNCNAME" && bash_dynamic_variables_enabled() &&
      funcname_frame_count() > 0) [[unlikely]]
  {
    let const depth = funcname_frame_count();
    let frames = ArrayList<String>{heap_allocator()};
    frames.reserve(depth);
    for (usize i = 0; i < depth; i++)
      frames.push_managed(funcname_frame_at(i));
    return frames;
  }

  if (is_associative_array(name)) return associative_values(name);

  let out = ArrayList<String>{heap_allocator()};
  if (const ArrayList<String> *array = lookup_indexed_array(name)) {
    out.reserve(array->count());
    for (const String &element : *array)
      out.push_managed(element.view());
    /* The sparse elements sit past the dense run, so appending them in index
       order yields the whole array in order. */
    for (sparse_array_entry &entry : collect_sparse_array_entries(
             m_sparse_array_values, name, scratch_allocator()))
      out.push(steal(entry.value));
    return out;
  }
  /* A plain scalar reads as a one-element array, the way bash treats $a as
     ${a[0]}, so "${a[@]}" of a scalar yields its single value. */
  if (Maybe<String> scalar = get_variable_value(name); scalar.has_value())
    out.push(steal(*scalar));
  return out;
}

fn EvalContext::array_element_is_set(StringView name,
                                     StringView subscript) throws -> bool
{
  if (subscript == "@" || subscript == "*") {
    return !collect_array_elements(name).is_empty();
  }
  if (is_associative_array(name))
    return lookup_associative_element(name, subscript).has_value();
  /* An indexed subscript is an arithmetic expression, so arr[1+1] resolves the
     way an indexed read would. A negative index counts from the end. */
  const i64 index = evaluate_arithmetic(subscript);
  if (const ArrayList<String> *array = lookup_indexed_array(name)) {
    const i64 array_count = static_cast<i64>(array->count());
    /* A negative index counts from the highest set index across the sparse
       elements, the same base the read path uses, so [[ -v a[-1] ]] names the
       element ${a[-1]} reads rather than a stale dense slot. */
    const i64 resolved =
        index < 0 ? index + array_negative_index_base(name) : index;
    if (resolved >= 0 && resolved < array_count) {
      return true;
    }
    /* An index past the dense run may name a sparsely-held element. */
    return resolved >= 0 &&
           m_sparse_array_values.find(
               sparse_array_key(name, static_cast<usize>(resolved),
                                scratch_allocator())
                   .view()) != nullptr;
  }
  /* A scalar answers for its sole index zero. */
  return index == 0 && get_variable_value(name).has_value();
}

fn EvalContext::matching_prefix_names(StringView prefix) const throws
    -> ArrayList<String>
{
  LOG(All, "listing variable names with the prefix '%.*s'",
      static_cast<int>(prefix.length), prefix.data);
  let names = ArrayList<String>{heap_allocator()};
  let seen = HashSet{heap_allocator()};
  auto do_consider = [&](StringView candidate) throws {
    if (candidate.starts_with(prefix) && !seen.contains(candidate)) {
      seen.add(candidate);
      names.push_managed(candidate);
    }
  };
  m_shell_variables.for_each([&](StringView variable_name, const String &v) {
    unused(v);
    do_consider(variable_name);
  });
  for (const String &environment_name : os::environment_names())
    do_consider(environment_name.view());
  utils::sort_ascending(names);
  return names;
}

fn EvalContext::collect_array_subscripts(StringView name) const throws
    -> ArrayList<String>
{
  let out = ArrayList<String>{heap_allocator()};
  if (is_associative_array(name)) {
    for (const String &key : associative_keys(name))
      out.push(String{heap_allocator(), key.view()});
    return out;
  }
  if (let const *array = lookup_indexed_array(name)) {
    for (usize i = 0; i < array->count(); i++)
      out.push(utils::uint_to_text(i));
    for (const sparse_array_entry &entry : collect_sparse_array_entries(
             m_sparse_array_values, name, scratch_allocator()))
      out.push(utils::uint_to_text(entry.index));
    return out;
  }
  /* A scalar has the single index zero, an unset name has none. */
  if (get_variable_value(name).has_value())
    out.push(String{heap_allocator(), "0"});
  return out;
}

} /* namespace shit */
