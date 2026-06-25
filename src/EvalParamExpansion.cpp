#include "Arena.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* The parameter expansion of the evaluator, the ${...} modifier dispatch
   with its default, alternate, trim, substring, replacement, and case
   forms, and the modifier-word expansion they share. */

namespace shit {

namespace {

enum class trim_end
{
  Prefix,
  Suffix,
};

/* Remove the shortest or longest prefix or suffix of value that matches pattern
   as a glob. The active mask marks which pattern bytes may act as glob
   metacharacters, so a quoted or escaped * or ? matches itself. */
fn trim_matching(Allocator result_allocator, StringView value,
                 StringView pattern, const ArrayList<bool> &active,
                 trim_end end, bool longest, bool extglob_enabled) throws
    -> String
{
  ASSERT(active.count() == pattern.length);

  if (end == trim_end::Prefix) {
    /* The longest match scans down from the whole string and the shortest up
       from the empty prefix, so the first hit is the wanted length. */
    if (longest) {
      for (usize length = value.length;; length--) {
        if (utils::glob_matches(pattern, value.substring_of_length(0, length),
                                active, 0, extglob_enabled))
          return String{result_allocator, value.substring(length)};
        if (length == 0) break;
      }
    } else {
      for (usize length = 0; length <= value.length; length++) {
        if (utils::glob_matches(pattern, value.substring_of_length(0, length),
                                active, 0, extglob_enabled))
          return String{result_allocator, value.substring(length)};
      }
    }

  } else {
    /* The longest match scans the suffix start up from byte zero and the
       shortest down from the end, so the first hit is the wanted start. */
    if (longest) {
      for (usize start = 0; start <= value.length; start++) {
        if (utils::glob_matches(pattern, value.substring(start), active, 0,
                                extglob_enabled))
          return String{result_allocator, value.substring_of_length(0, start)};
      }
    } else {
      for (usize start = value.length;; start--) {
        if (utils::glob_matches(pattern, value.substring(start), active, 0,
                                extglob_enabled))
          return String{result_allocator, value.substring_of_length(0, start)};
        if (start == 0) break;
      }
    }
  }
  return String{result_allocator, value};
}

/* The shared core of the # and % prefix and suffix trims, so the scalar and the
   array-element path expand the pattern with its glob mask through one place.
 */
static fn trim_value_with_modifier(EvalContext &cxt, StringView value,
                                   StringView word, trim_end end,
                                   bool longest) throws -> String
{
  LOG(All, "trimming a value of %zu bytes with the pattern word '%.*s'",
      value.length, static_cast<int>(word.length), word.data);
  let active = ArrayList<bool>{cxt.scratch_allocator()};
  let const pattern = cxt.expand_modifier_word_masked(word, active);
  return trim_matching(cxt.scratch_allocator(), value, pattern.view(), active,
                       end, longest, cxt.extglob_enabled());
}

} // namespace

fn EvalContext::expand_modifier_word(StringView word, bool remove_quotes) throws
    -> String
{
  /* The default, assign, alternate, error, and arithmetic forms never glob, so
     the mask the worker fills is discarded. The default word keeps a backslash
     before an ordinary character, so the pattern-only unescape stays off. */
  let discarded_mask = ArrayList<bool>{scratch_allocator()};
  return expand_modifier_word_worker(word, discarded_mask, remove_quotes,
                                     false);
}

fn EvalContext::expand_modifier_word_masked(StringView word,
                                            ArrayList<bool> &active_out,
                                            bool remove_quotes) throws -> String
{
  /* A # or % pattern word has every backslash quote the next byte. */
  return expand_modifier_word_worker(word, active_out, remove_quotes, true);
}

fn EvalContext::expand_modifier_word_worker(StringView word,
                                            ArrayList<bool> &active_out,
                                            bool remove_quotes,
                                            bool is_pattern_word) throws
    -> String
{
  LOG(All, "expanding a modifier word of %zu bytes", word.length);
  let out = String{scratch_allocator()};

  /* The mask stays parallel to out. */
  let const do_emit_byte = [&](char byte, bool is_active) {
    out += byte;
    active_out.push(is_active);
  };

  let do_emit_run = [&](StringView bytes, bool is_active) {
    out.append(bytes);
    for (usize k = 0; k < bytes.length; k++)
      active_out.push(is_active);
  };

  let is_in_single_quote = false;
  let is_in_double_quote = false;
  for (usize i = 0; i < word.length; i++) {
    /* In a default or a pattern word the quotes are removed, so ${x%"$suffix"}
       matches the value of suffix literally. A heredoc body passes
       remove_quotes as false. */
    if (remove_quotes && !is_in_single_quote && word[i] == '"') {
      is_in_double_quote = !is_in_double_quote;
      continue;
    }
    if (remove_quotes && !is_in_double_quote && word[i] == '\'') {
      is_in_single_quote = !is_in_single_quote;
      continue;
    }
    if (is_in_single_quote) {
      do_emit_byte(word[i], false);
      continue;
    }

    /* A backslash escapes the next byte from expansion before a dollar,
       backtick, backslash, or a double quote in a quote-stripping word, and is
       a line continuation before a newline. Any other backslash is kept
       literally. */
    if (word[i] == '\\') {
      /* In a # or % pattern word a backslash quotes the next byte, emitted
         literally and inactive with the backslash dropped, so a quoted glob
         character such as \* matches itself. */
      if (is_pattern_word && i + 1 < word.length) {
        do_emit_byte(word[i + 1], false);
        i++;
        continue;
      }
      if (i + 1 < word.length) {
        const char next = word[i + 1];
        if (next == '$' || next == '`' || next == '\\' ||
            (remove_quotes && next == '"'))
        {
          do_emit_byte(next, false);
          i++;
          continue;
        }
        if (next == '\n') {
          i++;
          continue;
        }
      }
      do_emit_byte('\\', false);
      continue;
    }

    if (word[i] == '`') {
      /* Old-style backtick command substitution, run to the next unescaped
         backtick. The POSIX backquote unescaping strips a backslash that
         precedes a backtick, a dollar sign, or another backslash. */
      let inner = String{scratch_allocator()};
      usize j = i + 1;
      for (; j < word.length; j++) {
        if (word[j] == '\\' && j + 1 < word.length &&
            (word[j + 1] == '`' || word[j + 1] == '$' || word[j + 1] == '\\'))
        {
          inner += word[j + 1];
          j++;
          continue;
        }
        if (word[j] == '`') break;
        inner += word[j];
      }
      do_emit_run(capture_command_substitution(inner), !is_in_double_quote);
      i = j;
      continue;
    }

    if (word[i] != '$') {
      do_emit_byte(word[i], !is_in_double_quote);
      continue;
    }
    if (i + 1 >= word.length) {
      do_emit_byte('$', !is_in_double_quote);
      break;
    }

    let const next = word[i + 1];
    if (next == '{') {
      /* Scan the ${...} body to the matching } at brace depth one. A nested
         ${...} bumps the depth, a nested $(...) is copied by quote-aware paren
         balance, and a quote run or a backslash escape keeps its bytes literal
         so a } inside is never counted. The structure mirrors the Lexer brace
         scanner. */
      let inner = String{scratch_allocator()};
      usize j = i + 2;
      i32 depth = 1;
      char quote = 0;
      while (j < word.length) {
        const char ch = word[j];
        if (quote != 0) {
          inner += ch;
          if (quote == '"' && ch == '\\' && j + 1 < word.length) {
            inner += word[++j];
            j++;
            continue;
          }
          if (ch == quote) quote = 0;
          j++;
          continue;
        }
        if (ch == '\\' && j + 1 < word.length) {
          inner += ch;
          inner += word[++j];
          j++;
          continue;
        }
        if (ch == '\'' || ch == '"') {
          quote = ch;
          inner += ch;
          j++;
          continue;
        }
        if (ch == '`') {
          inner += ch;
          j++;
          while (j < word.length) {
            const char b = word[j];
            inner += b;
            j++;
            if (b == '\\' && j < word.length) {
              inner += word[j++];
              continue;
            }
            if (b == '`') break;
          }
          continue;
        }
        if (ch == '$' && j + 1 < word.length && word[j + 1] == '(') {
          inner += ch;
          inner += word[++j];
          j++;
          usize paren_depth = 1;
          char nested_quote = 0;
          while (j < word.length) {
            const char p = word[j];
            inner += p;
            j++;
            if (nested_quote != 0) {
              if (nested_quote == '"' && p == '\\' && j < word.length) {
                inner += word[j++];
                continue;
              }
              if (p == nested_quote) nested_quote = 0;
              continue;
            }
            if (p == '\\' && j < word.length) {
              inner += word[j++];
              continue;
            }
            if (p == '\'' || p == '"') {
              nested_quote = p;
            } else if (p == '(') {
              paren_depth++;
            } else if (p == ')') {
              paren_depth--;
              if (paren_depth == 0) break;
            }
          }
          continue;
        }
        if (ch == '$' && j + 1 < word.length && word[j + 1] == '{') {
          depth++;
          inner += ch;
          inner += word[++j];
          j++;
          continue;
        }
        if (ch == '}') {
          depth--;
          if (depth == 0) break;
          inner += ch;
          j++;
          continue;
        }
        inner += ch;
        j++;
      }
      do_emit_run(apply_parameter_expansion(inner), !is_in_double_quote);
      i = j;
    } else if (lexer::is_variable_name_start(next)) {
      let name = String{scratch_allocator()};
      usize j = i + 1;
      while (j < word.length && lexer::is_variable_name(word[j]))
        name += word[j++];
      /* A nested reference obeys set -u the way a top level reference does, so
         an unset name here aborts rather than expanding to nothing, or warns
         under -W. */
      if (!get_variable_value(name).has_value()) report_unset_reference(name);
      /* An ordinary name appends its stored value with no temporary String. */
      if (const String *stored = lookup_shell_variable(name); stored != nullptr)
        do_emit_run(stored->view(), !is_in_double_quote);
      else
        do_emit_run(expand_variable(name), !is_in_double_quote);
      i = j - 1;
    } else if (next == '(' && i + 2 < word.length && word[i + 2] == '(') {
      /* Arithmetic $((...)), scanned to the matching )). A quote run or a
         backslash escape keeps its bytes literal so a ) inside a string does
         not count toward the depth. */
      let inner = String{scratch_allocator()};
      usize j = i + 3;
      usize depth = 0;
      char quote = 0;
      for (; j < word.length; j++) {
        const char ch = word[j];
        if (quote != 0) {
          inner += ch;
          if (quote == '"' && ch == '\\' && j + 1 < word.length) {
            inner += word[++j];
            continue;
          }
          if (ch == quote) quote = 0;
          continue;
        }
        if (ch == '\\' && j + 1 < word.length) {
          inner += ch;
          inner += word[++j];
          continue;
        }
        if (ch == '\'' || ch == '"') {
          quote = ch;
        } else if (ch == '(') {
          depth++;
        } else if (ch == ')' && depth > 0) {
          depth--;
        } else if (ch == ')' && j + 1 < word.length && word[j + 1] == ')') {
          j += 2;
          break;
        }
        inner += ch;
      }
      /* An arithmetic result cannot glob, so the bytes are emitted inactive. */
      do_emit_run(utils::int_to_text(evaluate_arithmetic(inner)), false);
      i = j - 1;
    } else if (next == '(') {
      /* Command substitution $(...), scanned to the matching ). A quote run or
         a backslash escape keeps its bytes literal so a ) inside a string does
         not close the substitution early. */
      let inner = String{scratch_allocator()};
      usize j = i + 2;
      usize depth = 1;
      char quote = 0;
      for (; j < word.length; j++) {
        const char ch = word[j];
        if (quote != 0) {
          inner += ch;
          if (quote == '"' && ch == '\\' && j + 1 < word.length) {
            inner += word[++j];
            continue;
          }
          if (ch == quote) quote = 0;
          continue;
        }
        if (ch == '\\' && j + 1 < word.length) {
          inner += ch;
          inner += word[++j];
          continue;
        }
        if (ch == '\'' || ch == '"') {
          quote = ch;
        } else if (ch == '(') {
          depth++;
        } else if (ch == ')') {
          depth--;
          if (depth == 0) break;
        }
        inner += ch;
      }
      do_emit_run(capture_command_substitution(inner), !is_in_double_quote);
      i = j;
    } else if (next == '?' || next == '@' || next == '*' || next == '#' ||
               next == '$' || next == '!' || next == '-' ||
               lexer::is_number(next))
    {
      let const special_name = StringView{&next, 1};
      if (!get_variable_value(special_name).has_value())
        report_unset_reference(special_name);
      do_emit_run(expand_variable(special_name), !is_in_double_quote);
      i++;
    } else {
      do_emit_byte('$', !is_in_double_quote);
    }
  }
  return out;
}

hot fn EvalContext::apply_parameter_expansion(StringView spec) throws -> String
{
  LOG(All, "applying the parameter expansion '${%.*s}'",
      static_cast<int>(spec.length), spec.data);

  /* A nested ${name:-${...}} default re-enters this dispatch at each level, so
     the depth is capped before the native stack is exhausted. */
  enter_parameter_expansion();
  defer { leave_parameter_expansion(); };

  if (spec.is_empty()) return String{scratch_allocator()};

  /* ${!name} reads the value of the variable that name names, or lists the
     variable names that start with a prefix when it ends with * or @. */
  if (spec.length > 1 && spec[0] == '!') {
    let const body = spec.substring(1);
    /* A modifier after the name applies to the indirected value, so the name
       splits off and the rewritten spec runs through this same dispatch. The
       bare trailing * and @ stay with the body as the prefix-listing forms, and
       a [subscript] stays glued to the name. */
    usize name_end = 0;
    while (name_end < body.length && lexer::is_variable_name(body[name_end]))
      name_end++;
    if (name_end > 0 && name_end < body.length && body[name_end] == '[') {
      if (let const close = body.substring(name_end).find_character(']'))
        name_end += *close + 1;
    }
    if (name_end > 0 && name_end < body.length &&
        !(name_end == body.length - 1 &&
          (body[name_end] == '*' || body[name_end] == '@')))
    {
      let const name = body.substring_of_length(0, name_end);
      let const target = get_variable_value(name);
      let rewritten = String{scratch_allocator()};
      rewritten.reserve(body.length + name.length);
      /* bash makes an unset indirection name with a modifier an "invalid
         indirect expansion" failure. A fatal error here would be harsher than
         bash, so the unset name stands in for the target and the modifier sees
         the unset state. */
      rewritten.append(target.has_value() ? StringView{target->view()} : name);
      rewritten.append(body.substring(name_end));
      return apply_parameter_expansion(rewritten.view());
    }
    return apply_indirect_or_name_listing(body);
  }

  /* ${#name} is the value length, distinct from $# the positional count. */
  if (spec.length > 1 && spec[0] == '#') {
    let const name = spec.substring(1);
    if (name == "@" || name == "*")
      return String{scratch_allocator(),
                    utils::uint_to_text(m_positional_params.count())};

    /* ${#a[@]} is the element count, ${#a[i]} the length of one element. */
    if (let const bracket = name.find_character('[');
        bracket.has_value() && *bracket > 0 && name[name.length - 1] == ']' &&
        lexer::is_variable_name_start(name[0]))
    {
      const StringView array_name = name.substring_of_length(0, *bracket);
      const StringView subscript =
          name.substring_of_length(*bracket + 1, name.length - *bracket - 2);
      if (subscript == "@" || subscript == "*") {
        if ((array_name == "FUNCNAME" || array_name == "BASH_LINENO") &&
            bash_dynamic_variables_enabled()) [[unlikely]]
        {
          return String{scratch_allocator(),
                        utils::uint_to_text(funcname_frame_count())};
        }
        if (is_associative_array(array_name))
          return String{
              heap_allocator(),
              utils::uint_to_text(associative_keys(array_name).count())};
        if (lookup_indexed_array(array_name) != nullptr)
          return utils::uint_to_text(
              collect_array_elements(array_name).count());
        return String{scratch_allocator(),
                      utils::uint_to_text(
                          get_variable_value(array_name).has_value() ? 1 : 0)};
      }
      return String{scratch_allocator(),
                    utils::uint_to_text(
                        apply_array_subscript(array_name, subscript).length())};
    }

    let const value = get_variable_value(name);
    if (!value.has_value()) report_unset_reference(name);
    return String{scratch_allocator(),
                  utils::uint_to_text(value.value_or(String{}).length())};
  }

  ASSERT(!spec.is_empty());
  usize name_end = 0;
  if (lexer::is_variable_name_start(spec[0])) {
    while (name_end < spec.length && lexer::is_variable_name(spec[name_end]))
      name_end++;
  } else if (lexer::is_number(spec[0])) {
    while (name_end < spec.length && lexer::is_number(spec[name_end]))
      name_end++;
  } else {
    name_end = 1;
  }

  let const name = spec.substring_of_length(0, name_end);
  let const rest = spec.substring(name_end);

  /* ${name[subscript]} reads one indexed-array element, or every element when
     the subscript is @ or *. */
  if (!rest.is_empty() && rest[0] == '[' && !name.is_empty() &&
      lexer::is_variable_name_start(name[0]))
  {
    if (let const close = rest.find_character(']'); close.has_value()) {
      const StringView subscript = rest.substring_of_length(1, *close - 1);
      if (*close + 1 == rest.length)
        return apply_array_subscript(name, subscript);
      /* A value-transform modifier after the ], the / replacement, the # and %
         trims, or the ^ and , case changes, modifies the one element. A
         different modifier such as :- falls through to the general path. */
      const StringView modifier = rest.substring(*close + 1);
      const char modifier_op = modifier.is_empty() ? '\0' : modifier[0];
      if (subscript != "@" && subscript != "*" &&
          (modifier_op == '/' || modifier_op == '#' || modifier_op == '%' ||
           modifier_op == '^' || modifier_op == ','))
      {
        return apply_value_modifier(
            apply_array_subscript(name, subscript).view(), modifier);
      }
      /* The = form assigns the element back, the others test its own setness.
       */
      if (subscript != "@" && subscript != "*" && !modifier.is_empty()) {
        let const is_colon = modifier_op == ':';
        let const after =
            is_colon && modifier.length > 1 ? modifier[1] : modifier_op;
        let const is_test_form =
            after == '-' || after == '+' || after == '=' || after == '?';
        if (is_colon && !is_test_form) {
          return apply_substring_to_value(
              apply_array_subscript(name, subscript).view(),
              modifier.substring(1));
        }
        if (is_test_form) {
          let const element_is_set = array_element_is_set(name, subscript);
          let const value = element_is_set
                                ? apply_array_subscript(name, subscript)
                                : String{scratch_allocator()};
          let const treat_as_unset =
              is_colon ? value.is_empty() : !element_is_set;
          let const word = modifier.substring(is_colon ? 2 : 1);
          switch (after) {
          case '-':
            if (treat_as_unset) return expand_modifier_word(word);
            return value;
          case '+':
            if (treat_as_unset) return String{scratch_allocator()};
            return expand_modifier_word(word);
          case '=': {
            if (!treat_as_unset) return value;
            let const assigned = expand_modifier_word(word);
            assign_array_element(name, subscript, assigned.view(), false);
            return assigned;
          }
          case '?':
            if (treat_as_unset) {
              if (word.is_empty())
                throw_script_fatal(
                    "Unable to expand '" + name + "[" + subscript +
                    "]' because the element is not set or is empty");
              throw_script_fatal(expand_modifier_word(word));
            }
            return value;
          default: break;
          }
        }
      }
    }
  }

  if (rest.is_empty()) {
    /* Under set -u a plain reference to an unset variable is an error, while a
       form with a modifier such as ${x:-w} handles the unset case itself. */
    if (!get_variable_value(name).has_value()) report_unset_reference(name);
    return expand_variable(name);
  }

  /* A leading colon makes the test forms treat an empty value as unset. */
  let const is_colon_form = rest[0] == ':';
  const usize op_index = is_colon_form ? 1 : 0;
  if (op_index >= rest.length) return expand_variable(name);

  /* ${name:offset:length} is bash substring expansion, the colon form whose
     character after the colon is not a - = + ? modifier. A name of @ or * is a
     positional slice. */
  if (is_colon_form) {
    const char after_colon = rest[op_index];
    if (after_colon != '-' && after_colon != '=' && after_colon != '+' &&
        after_colon != '?' && name != "@" && name != "*")
    {
      return apply_substring_expansion(name, rest.substring(1));
    }
  }

  /* ${name/pat/rep} and its // # % variants are bash pattern replacement, the
     non-colon form whose operator is a slash. A name of @ or * applies per
     element. */
  if (!is_colon_form && rest[0] == '/' && name != "@" && name != "*")
    return apply_pattern_replacement(name, rest);

  /* ${name^}, ${name,}, and the doubled and tilde forms are bash case
     modification, the non-colon caret, comma, or tilde operator. */
  if (!is_colon_form && (rest[0] == '^' || rest[0] == ',' || rest[0] == '~') &&
      name != "@" && name != "*")
  {
    return apply_case_modification(name, rest);
  }

  /* ${name@op} is a bash parameter transform absent from the sh mood. */
  if (!is_colon_form && rest[0] == '@' && rest.length >= 2 &&
      mood() != mimic_mood::Posix && name != "@" && name != "*")
  {
    return apply_parameter_transform(name, rest[1]);
  }

  let const op = rest[op_index];
  let const is_doubled = (op_index + 1 < rest.length &&
                          rest[op_index + 1] == op && (op == '#' || op == '%'));
  let const word = rest.substring(op_index + (is_doubled ? 2 : 1));

  /* A subscripted name tests and reads its element, while a bare name reads
     element zero through the ordinary lookup. */
  let current = Maybe<String>{};
  if (let const bracket = name.find_character('[');
      bracket.has_value() && name[name.length - 1] == ']')
  {
    let const array_name = name.substring_of_length(0, *bracket);
    let const subscript =
        name.substring_of_length(*bracket + 1, name.length - *bracket - 2);
    if (array_element_is_set(array_name, subscript))
      current = apply_array_subscript(array_name, subscript);
  } else {
    current = get_variable_value(name);
  }
  let const is_set = current.has_value();
  let const is_empty = !is_set || current->is_empty();
  let const treat_as_unset = is_colon_form ? is_empty : !is_set;

  switch (op) {
  case '-':
    if (treat_as_unset) return expand_modifier_word(word);
    ASSERT(current.has_value());
    return String{scratch_allocator(), current->view()};
  case '=':
    if (treat_as_unset) {
      let const assigned = expand_modifier_word(word);
      set_shell_variable(name, assigned);
      return assigned;
    }
    ASSERT(current.has_value());
    return String{scratch_allocator(), current->view()};
  case '+':
    if (treat_as_unset) return String{scratch_allocator()};
    return expand_modifier_word(word);
  case '?':
    if (treat_as_unset) {
      if (word.is_empty())
        throw_script_fatal("Unable to expand '" + name +
                           "' because the parameter is not set or is empty");
      throw_script_fatal(expand_modifier_word(word));
    }
    ASSERT(current.has_value());
    return String{scratch_allocator(), current->view()};

  case '#': {
    return trim_value_with_modifier(*this, current.value_or(String{}).view(),
                                    word, trim_end::Prefix, is_doubled);
  }

  case '%': {
    return trim_value_with_modifier(*this, current.value_or(String{}).view(),
                                    word, trim_end::Suffix, is_doubled);
  }

  default: return expand_variable(name);
  }
}

/* The index of the colon that separates the offset from the length, or the body
   length when there is none. Parentheses and a ternary inside the offset are
   tracked so a colon belonging to a ternary is not mistaken for it. */
fn find_substring_length_separator(StringView body) wontthrow -> usize
{
  usize paren_depth = 0;
  usize question_depth = 0;
  for (usize i = 0; i < body.length; i++) {
    const char character = body[i];
    if (character == '(') {
      paren_depth++;
    } else if (character == ')') {
      if (paren_depth > 0) paren_depth--;
    } else if (character == '?' && paren_depth == 0) {
      question_depth++;
    } else if (character == ':' && paren_depth == 0) {
      if (question_depth > 0)
        question_depth--;
      else
        return i;
    }
  }
  return body.length;
}

fn EvalContext::apply_substring_expansion(StringView name,
                                          StringView body) throws -> String
{
  let const current = get_variable_value(name);
  if (m_runtime.error_unset && !current.has_value())
    throw_script_fatal("Unable to expand '" + name +
                       "' because the parameter is not set");
  return apply_substring_to_value(current.value_or(String{}).view(), body);
}

fn EvalContext::apply_substring_to_value(StringView value,
                                         StringView body) throws -> String
{
  LOG(All, "taking the substring '%.*s' of a value of %zu bytes",
      static_cast<int>(body.length), body.data, value.length);
  const i64 value_length = static_cast<i64>(value.length);

  let const separator = find_substring_length_separator(body);
  let const offset_text = body.substring_of_length(0, separator);
  const i64 offset =
      offset_text.is_empty() ? 0 : evaluate_arithmetic(offset_text);

  /* A negative offset counts from the end. An offset still before the start
     yields nothing, the way bash returns empty rather than clamping to the
     whole value. A positive offset past the end clamps to the end. */
  i64 start = offset < 0 ? value_length + offset : offset;
  if (start < 0) return String{scratch_allocator()};
  if (start > value_length) start = value_length;

  i64 end = value_length;
  if (separator < body.length) {
    let const length_text = body.substring(separator + 1);
    i64 length = length_text.is_empty() ? 0 : evaluate_arithmetic(length_text);
    if (length < 0) {
      /* A negative length names a position counted back from the end, so the
         substring runs up to that point. */
      end = value_length + length;
    } else {
      /* The length is clamped first to keep the start-plus-length sum from
         overflowing i64. */
      if (length > value_length) length = value_length;
      end = start + length;
    }
  }
  if (end > value_length) end = value_length;
  /* A length that resolves to a point before the offset is the bash
     "substring expression < 0" error, fatal. */
  if (end < start)
    throw Error{"Unable to take the substring because the length names "
                "a point before the offset"};

  return String{scratch_allocator(),
                value.substring_of_length(static_cast<usize>(start),
                                          static_cast<usize>(end - start))};
}

/* The index of the unescaped slash that separates the pattern from the
   replacement, or the body length when there is none. A slash inside a quote
   run or behind a backslash belongs to the pattern, the way bash reads
   ${var/#"a/b"/c}, so the scan tracks the quote state. */
static fn find_replacement_separator(StringView body) wontthrow -> usize
{
  char quote = 0;
  for (usize i = 0; i < body.length; i++) {
    let const character = body[i];
    if (quote == '\'') {
      if (character == '\'') quote = 0;
      continue;
    }
    if (character == '\\') {
      i++;
      continue;
    }
    if (quote == '"') {
      if (character == '"') quote = 0;
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
      continue;
    }
    if (character == '/') return i;
  }
  return body.length;
}

/* The length of the longest match the pattern makes starting at the given
   position, or None when it matches nothing there. The end shrinks from the
   value end so a greedy star takes the most it can. */
static fn longest_pattern_match_at(StringView pattern,
                                   const ArrayList<bool> &pattern_active,
                                   StringView value, usize start,
                                   bool extglob) throws -> Maybe<usize>
{
  for (usize end = value.length; end >= start; end--) {
    if (utils::glob_matches(pattern,
                            value.substring_of_length(start, end - start),
                            pattern_active, 0, extglob))
      return end - start;
    if (end == start) break;
  }
  return None;
}

fn EvalContext::apply_pattern_replacement(StringView name,
                                          StringView spec) throws -> String
{
  let const current = get_variable_value(name);
  if (m_runtime.error_unset && !current.has_value())
    throw_script_fatal("Unable to expand '" + name +
                       "' because the parameter is not set");
  return pattern_replace_value(current.value_or(String{}), spec);
}

/* The replacement text of ${x/pat/rep} reads & as the matched span and a
   backslash as an escape, so \& is a literal & and a backslash before any other
   byte is dropped, the way bash splices a match. */
static fn append_pattern_replacement(String &out, StringView replacement,
                                     StringView matched) throws -> void
{
  for (usize i = 0; i < replacement.length; i++) {
    if (replacement[i] == '\\' && i + 1 < replacement.length) {
      out.push(replacement[i + 1]);
      i++;
    } else if (replacement[i] == '&') {
      out.append(matched);
    } else {
      out.push(replacement[i]);
    }
  }
}

fn EvalContext::pattern_replace_value(const String &value,
                                      StringView spec) throws -> String
{
  LOG(All, "applying the pattern replacement '%.*s' to a value of %zu bytes",
      static_cast<int>(spec.length), spec.data, value.count());
  /* A doubled slash replaces every match, and a # or % after the first slash
     anchors the pattern to the start or the end. */
  StringView remainder = spec.substring(1);
  bool should_replace_all = false;
  bool is_anchored_at_start = false;
  bool is_anchored_at_end = false;
  if (!remainder.is_empty() && remainder[0] == '/') {
    should_replace_all = true;
    remainder = remainder.substring(1);
  } else if (!remainder.is_empty() && remainder[0] == '#') {
    is_anchored_at_start = true;
    remainder = remainder.substring(1);
  } else if (!remainder.is_empty() && remainder[0] == '%') {
    is_anchored_at_end = true;
    remainder = remainder.substring(1);
  }

  const usize separator = find_replacement_separator(remainder);
  let pattern_active = ArrayList<bool>{scratch_allocator()};
  let const pattern = expand_modifier_word_masked(
      remainder.substring_of_length(0, separator), pattern_active);
  /* No separator means the replacement is empty, so the matches are deleted. */
  let const replacement =
      separator < remainder.length
          ? expand_modifier_word(remainder.substring(separator + 1))
          : String{heap_allocator()};

  /* An empty unanchored pattern matches nothing in bash, so the value is
     returned unchanged. The anchored forms still splice at the start or the
     end. */
  if (pattern.is_empty() && !is_anchored_at_start && !is_anchored_at_end)
    return value;

  let out = String{scratch_allocator()};

  /* The start anchor matches a prefix, so a single longest match at the front
     is replaced and the rest kept. */
  if (is_anchored_at_start) {
    if (let const matched = longest_pattern_match_at(
            pattern.view(), pattern_active, value.view(), 0, extglob_enabled()))
    {
      append_pattern_replacement(out, replacement.view(),
                                 value.view().substring_of_length(0, *matched));
      out.append(value.view().substring(*matched));
    } else {
      out.append(value.view());
    }
    return out;
  }

  /* The end anchor matches a suffix, so the leftmost start whose remainder
     fully matches names the longest matching suffix to replace. */
  if (is_anchored_at_end) {
    for (usize start = 0; start <= value.length(); start++) {
      if (utils::glob_matches(pattern.view(), value.view().substring(start),
                              pattern_active, 0, extglob_enabled()))
      {
        out.append(value.view().substring_of_length(0, start));
        append_pattern_replacement(out, replacement.view(),
                                   value.view().substring(start));
        return out;
      }
    }
    out.append(value.view());
    return out;
  }

  /* The unanchored form replaces the first match for the single slash and every
     non-overlapping match for the doubled slash. A zero-length match advances
     one byte so the scan cannot loop. */
  bool has_replaced = false;
  usize i = 0;
  while (i < value.length()) {
    Maybe<usize> matched;
    if (!has_replaced || should_replace_all)
      matched = longest_pattern_match_at(pattern.view(), pattern_active,
                                         value.view(), i, extglob_enabled());
    if (matched.has_value()) {
      append_pattern_replacement(out, replacement.view(),
                                 value.view().substring_of_length(i, *matched));
      has_replaced = true;
      if (*matched == 0) {
        out.push(value.view()[i]);
        i++;
      } else {
        i += *matched;
      }
      if (!should_replace_all) {
        out.append(value.view().substring(i));
        return out;
      }
    } else {
      out.push(value.view()[i]);
      i++;
    }
  }
  return out;
}

/* Quote a value so it reads back as one shell word, the way the bash ${var@Q}
   transform does. An empty value becomes '', a control byte forces the $'...'
   form, and anything else is single-quoted with an embedded quote written as
   the '\'' break-out. */
static fn append_shell_quoted(String &out, StringView arg) throws -> void
{
  if (arg.is_empty()) {
    out += "''";
    return;
  }

  bool has_control_byte = false;
  for (usize i = 0; i < arg.length; i++) {
    let const byte = static_cast<unsigned char>(arg[i]);
    if (byte < 0x20 || byte == 0x7f) {
      has_control_byte = true;
      break;
    }
  }

  if (has_control_byte) {
    out += "$'";
    for (usize i = 0; i < arg.length; i++) {
      let const character = arg[i];
      switch (character) {
      case '\a': out += "\\a"; break;
      case '\b': out += "\\b"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\v': out += "\\v"; break;
      case '\f': out += "\\f"; break;
      case '\r': out += "\\r"; break;
      case '\x1b': out += "\\E"; break;
      case '\'': out += "\\'"; break;
      case '\\': out += "\\\\"; break;
      default: {
        let const byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7f) {
          out.push('\\');
          out.push(static_cast<char>('0' + ((byte >> 6) & 7)));
          out.push(static_cast<char>('0' + ((byte >> 3) & 7)));
          out.push(static_cast<char>('0' + (byte & 7)));
        } else {
          out.push(character);
        }
        break;
      }
      }
    }
    out += "'";
    return;
  }

  out.push('\'');
  for (usize i = 0; i < arg.length; i++) {
    if (arg[i] == '\'')
      out += "'\\''";
    else
      out.push(arg[i]);
  }
  out.push('\'');
}

/* The ${var@op} transform, a bash 5.3 family. Q quotes for reuse, U u L change
   the case, E expands backslash escapes, A prints a recreating assignment, and
   a lists the attribute letters. An operator with no handler yields the plain
   expansion. */
fn EvalContext::apply_parameter_transform(StringView name, char op) throws
    -> String
{
  let const value = get_variable_value(name);
  if (m_runtime.error_unset && !value.has_value())
    throw_script_fatal("Unable to expand '" + name +
                       "' because the parameter is not set");

  /* An unset variable transforms to empty, so ${unset@Q} is empty rather than
     the '' an empty-but-set value yields. */
  if (!value.has_value()) return String{scratch_allocator()};

  return apply_parameter_transform_to_value(value->view(), op, name);
}

/* The value-only core, shared by the scalar name path and the per-element array
   field mapping in "${a[@]@op}". The name backs the A assignment form and the a
   attribute listing. */
fn EvalContext::apply_parameter_transform_to_value(StringView text, char op,
                                                   StringView name) throws
    -> String
{
  let out = String{scratch_allocator()};
  out.reserve(text.length);
  switch (op) {
  case 'U':
    for (usize i = 0; i < text.length; i++)
      out.push(
          static_cast<char>(std::toupper(static_cast<unsigned char>(text[i]))));
    return out;
  case 'L':
    for (usize i = 0; i < text.length; i++)
      out.push(
          static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
    return out;
  case 'u':
    for (usize i = 0; i < text.length; i++) {
      char character = text[i];
      if (i == 0)
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
      out.push(character);
    }
    return out;
  case 'Q':
  case 'K':
  case 'k':
    /* On a bare name K and k quote the value the way Q does, matching bash. The
       key-and-value listing is the ${a[@]@K} array-field form on the element
       path. */
    append_shell_quoted(out, text);
    return out;
  case 'P':
    /* Expand the value as a prompt string with the PS1 backslash escapes. */
    return toiletline::expand_prompt_template(text, *this);
  case 'A':
    out.append(name);
    out += '=';
    append_shell_quoted(out, text);
    return out;
  case 'E':
    for (usize i = 0; i < text.length; i++) {
      if (text[i] != '\\' || i + 1 >= text.length) {
        out.push(text[i]);
        continue;
      }
      i++;
      switch (text[i]) {
      case 'n': out.push('\n'); break;
      case 't': out.push('\t'); break;
      case 'r': out.push('\r'); break;
      case 'a': out.push('\a'); break;
      case 'b': out.push('\b'); break;
      case 'f': out.push('\f'); break;
      case 'v': out.push('\v'); break;
      case 'e': out.push('\x1b'); break;
      case '\\': out.push('\\'); break;
      case '\'': out.push('\''); break;
      case '"': out.push('"'); break;
      default:
        out.push('\\');
        out.push(text[i]);
        break;
      }
    }
    return out;
  case 'a':
    if (lookup_indexed_array(name) != nullptr) out.push('a');
    if (is_associative_array(name)) out.push('A');
    if (is_integer_variable(name)) out.push('i');
    if (is_readonly(name)) out.push('r');
    if (is_exported(name)) out.push('x');
    return out;
  default: return expand_variable(name);
  }
}

fn EvalContext::apply_case_modification(StringView name, StringView spec) throws
    -> String
{
  let const current = get_variable_value(name);
  if (m_runtime.error_unset && !current.has_value())
    throw_script_fatal("Unable to expand '" + name +
                       "' because the parameter is not set");
  return apply_case_modification_to_value(current.value_or(String{}).view(),
                                          spec);
}

/* The value-only core of the case modification, shared by the array element and
   the scalar name path. */
fn EvalContext::apply_case_modification_to_value(StringView value,
                                                 StringView spec) throws
    -> String
{
  LOG(All, "applying the case modification '%.*s' to a value of %zu bytes",
      static_cast<int>(spec.length), spec.data, value.length);
  const char op = spec[0];
  /* A doubled operator touches every matching character, a single one only the
     first. */
  const bool should_modify_all = spec.length > 1 && spec[1] == op;
  const StringView pattern_word = spec.substring(should_modify_all ? 2 : 1);

  /* An omitted pattern matches every character, the bash default of ?. */
  let pattern_active = ArrayList<bool>{scratch_allocator()};
  String pattern;
  if (pattern_word.is_empty()) {
    pattern = String{scratch_allocator(), "?"};
    pattern_active.push(true);
  } else {
    pattern = expand_modifier_word_masked(pattern_word, pattern_active);
  }

  let out = String{scratch_allocator()};
  out.reserve(value.length);
  for (usize i = 0; i < value.length; i++) {
    char character = value[i];
    const bool is_affected = should_modify_all || i == 0;
    if (is_affected &&
        utils::glob_matches(pattern.view(), value.substring_of_length(i, 1),
                            pattern_active, 0, extglob_enabled()))
    {
      const unsigned char byte = static_cast<unsigned char>(character);
      if (op == '^') {
        character = static_cast<char>(std::toupper(byte));
      } else if (op == ',') {
        character = static_cast<char>(std::tolower(byte));
      } else {
        /* The tilde toggles the case and leaves a non-letter alone. */
        if (std::islower(byte) != 0)
          character = static_cast<char>(std::toupper(byte));
        else if (std::isupper(byte) != 0)
          character = static_cast<char>(std::tolower(byte));
      }
    }
    out.push(character);
  }
  return out;
}

/* Apply a trailing parameter-expansion modifier to a single value, the /
   replacement, the # and % trims, and the ^ and , case changes, so each maps
   over an array element the way bash does. A modifier byte that is not a value
   transform leaves the value unchanged. */
fn EvalContext::apply_value_modifier(StringView value,
                                     StringView modifier) throws -> String
{
  if (modifier.is_empty()) return String{scratch_allocator(), value};
  const char op = modifier[0];
  if (op == '/')
    return pattern_replace_value(String{scratch_allocator(), value}, modifier);
  if (op == '^' || op == ',')
    return apply_case_modification_to_value(value, modifier);
  if (op == '#' || op == '%') {
    const bool is_doubled = modifier.length > 1 && modifier[1] == op;
    const StringView pattern_word = modifier.substring(is_doubled ? 2 : 1);
    return trim_value_with_modifier(
        *this, value, pattern_word,
        op == '#' ? trim_end::Prefix : trim_end::Suffix, is_doubled);
  }
  return String{scratch_allocator(), value};
}

} // namespace shit
