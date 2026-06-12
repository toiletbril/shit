#include "Eval.hpp"

#include "Arena.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* The word expansion of the evaluator, the segment walk that emits fields
   with their glob masks, the assignment form that never splits, the case
   pattern form that keeps the mask, and the compgen -W word list split.
   Split out of Eval.cpp so the evaluator core stays the hot-path file. */

namespace shit {

hot fn EvalContext::expand_word(const Word &word) throws
    -> ArrayList<glob_field>
{
  LOG(verbosity::All, "expanding a word of %zu segments into fields",
      word.segments.count());
  let const scratch = scratch_allocator();

  /* Only copy the segments when a leading tilde must be rewritten. The common
     word has no tilde and reads its segments in place. The copy, when it
     happens, lives only until this word finishes expanding, so it goes on the
     scratch arena the command reclaims rather than the heap. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{scratch};
  if (!word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front(),
                 tilde_expanded_segments.count() > 1);
    segments = &tilde_expanded_segments;
  }

  let fields = ArrayList<glob_field>{scratch};
  let current = glob_field{scratch};
  let has_current = false;

  auto flush = [&]() {
    if (has_current) {
      fields.push(steal(current));
      current = glob_field{scratch};
      has_current = false;
    }
  };

  auto append_run = [&](StringView text, bool glob_active) {
    current.text.append(text);
    current.glob_active.reserve(current.glob_active.count() + text.length);
    for (usize k = 0; k < text.length; k++)
      current.glob_active.push(glob_active);
    has_current = true;
  };

  /* A field with no bytes is still pushed, which a non-whitespace IFS delimiter
     run needs so that an empty field between two delimiters survives. flush
     alone emits only a started field and so cannot stand in here. */
  auto emit_empty_field = [&]() { fields.push(glob_field{scratch}); };

  /* IFS whitespace folds and a non-whitespace IFS byte delimits one field each,
     matching dash. A single forward pass classifies every byte as a field byte,
     a whitespace separator, or a delimiter separator, and emits one field per
     run. A whitespace run that holds no delimiter ends the current field. A run
     that holds k delimiters ends the current field and emits k minus one empty
     fields, so a:b yields two fields and a::b yields an empty between them. A
     leading delimiter forces a leading empty field, and a trailing whitespace
     run or a trailing single delimiter leaves no empty field behind. */
  auto append_split_run = [&](StringView text, bool glob_active) {
    usize i = 0;
    while (i < text.length) {
      const char byte = text.data[i];
      if (!is_field_separator(byte)) {
        usize start = i;
        while (i < text.length && !is_field_separator(text.data[i]))
          i++;
        append_run(StringView{text.data + start, i - start}, glob_active);
        continue;
      }

      /* Count the delimiters inside the maximal separator run, since each one
         past the first marks an empty field. A whitespace byte only folds. */
      const bool was_field_started = has_current;
      usize delimiter_count = 0;
      while (i < text.length && is_field_separator(text.data[i])) {
        const char separator = text.data[i];
        if (separator != ' ' && separator != '\t' && separator != '\n')
          delimiter_count++;
        i++;
      }

      /* The accumulated field ends here whether the run folds or delimits. */
      flush();
      if (delimiter_count == 0) continue;

      /* The first delimiter that follows an empty field forces that empty field
         out, so a leading delimiter and a delimiter after another delimiter
         both keep their empty. A delimiter that closes a non-empty field adds
         no extra empty, since flush already emitted that field. Each further
         delimiter in the run marks one more empty field. */
      if (!was_field_started) emit_empty_field();
      for (usize k = 1; k < delimiter_count; k++)
        emit_empty_field();
    }
  };

  for (const WordSegment &segment : *segments) {
    let const segment_text =
        StringView{segment.text.data(), segment.text.count()};
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText:
    case WordSegment::Kind::DoubleQuotedText:
      append_run(segment_text, false);
      break;
    case WordSegment::Kind::UnquotedText:
      append_split_run(segment_text, true);
      break;
    case WordSegment::Kind::VariableReference: {
      /* "$@" expands to one field per positional parameter. The first joins any
         preceding text, the last leaves its field open for following text. */
      if (segment.text == "@" && segment.is_in_double_quotes) {
        for (usize i = 0; i < m_positional_params.count(); i++) {
          if (i > 0) flush();
          append_run(StringView{m_positional_params[i].data(),
                                m_positional_params[i].count()},
                     false);
        }
        break;
      }
      /* An unquoted $@ or $* keeps each positional parameter as its own field
         boundary, then field splits each parameter's own text under IFS.
         Routing it through a single joined string instead would lose the
         boundary, so a custom or an empty IFS would merge or mis-split the
         parameters. The quoted "$*" join by the first IFS character stays in
         the default branch below. */
      if ((segment.text == "@" || segment.text == "*") &&
          !segment.is_in_double_quotes)
      {
        for (usize i = 0; i < m_positional_params.count(); i++) {
          if (i > 0) flush();
          append_split_run(StringView{m_positional_params[i].data(),
                                      m_positional_params[i].count()},
                           true);
        }
        break;
      }
      /* "${!prefix@}" emits one field per matching variable name, the way "$@"
         and "${a[@]}" do, while "${!prefix*}" joins them by the first IFS
         character into one field. The general path returns a single
         space-joined string, which loses the per-name boundary, so the name
         listing is emitted here. The form is a leading '!' and a trailing '@'
         or '*', which the indirect ${!ref} and the array-key ${!a[@]} do not
         take. */
      if (segment_text.length >= 2 && segment_text[0] == '!' &&
          (segment_text[segment_text.length - 1] == '@' ||
           segment_text[segment_text.length - 1] == '*'))
      {
        const StringView prefix =
            segment_text.substring_of_length(1, segment_text.length - 2);
        let const is_star = segment_text[segment_text.length - 1] == '*';
        let const names = matching_prefix_names(prefix);
        if (segment.is_in_double_quotes && is_star) {
          let const ifs = m_field_separators.view();
          let joined = String{scratch_allocator()};
          for (usize i = 0; i < names.count(); i++) {
            if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
            joined.append(names[i].view());
          }
          append_run(joined, false);
        } else if (segment.is_in_double_quotes) {
          for (usize i = 0; i < names.count(); i++) {
            if (i > 0) flush();
            append_run(names[i].view(), false);
          }
        } else {
          for (usize i = 0; i < names.count(); i++) {
            if (i > 0) flush();
            append_split_run(names[i].view(), true);
          }
        }
        break;
      }
      /* "${!a[@]}" emits one field per subscript, the way "${a[@]}" emits one
         per element, while "${!a[*]}" joins them by the first IFS character.
         The joined string path loses the per-subscript boundary, so the field
         form is produced here. The body is a leading '!' and a trailing [@] or
         [*]. */
      if (segment_text.length >= 5 && segment_text[0] == '!' &&
          segment_text[segment_text.length - 1] == ']' &&
          segment_text[segment_text.length - 3] == '[' &&
          (segment_text[segment_text.length - 2] == '@' ||
           segment_text[segment_text.length - 2] == '*') &&
          lexer::is_variable_name_start(segment_text[1]))
      {
        const StringView array_name =
            segment_text.substring_of_length(1, segment_text.length - 4);
        let const is_star = segment_text[segment_text.length - 2] == '*';
        let const subscripts = collect_array_subscripts(array_name);
        if (segment.is_in_double_quotes && is_star) {
          let const ifs = m_field_separators.view();
          let joined = String{scratch_allocator()};
          for (usize i = 0; i < subscripts.count(); i++) {
            if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
            joined.append(subscripts[i].view());
          }
          append_run(joined, false);
        } else if (segment.is_in_double_quotes) {
          for (usize i = 0; i < subscripts.count(); i++) {
            if (i > 0) flush();
            append_run(subscripts[i].view(), false);
          }
        } else {
          for (usize i = 0; i < subscripts.count(); i++) {
            if (i > 0) flush();
            append_split_run(subscripts[i].view(), true);
          }
        }
        break;
      }
      /* "${a[@]}" emits one field per array element, the way "$@" does for the
         positional parameters, while "${a[*]}" joins them by the first IFS
         character into one field. An unquoted ${a[@]} or ${a[*]} keeps each
         element its own field and splits it under IFS. The general path below
         joins to a single string, which would lose the per-element boundary, so
         the array @ and * forms are emitted here. A spec with a trailing
         modifier does not end in ']' and falls through. */
      if (segment_text.length >= 4 &&
          segment_text[segment_text.length - 1] == ']' &&
          segment_text[segment_text.length - 3] == '[' &&
          (segment_text[segment_text.length - 2] == '@' ||
           segment_text[segment_text.length - 2] == '*') &&
          lexer::is_variable_name_start(segment_text[0]))
      {
        let const array_name =
            segment_text.substring_of_length(0, segment_text.length - 3);
        let is_plain_array_name = true;
        for (usize i = 0; i < array_name.length; i++)
          if (!lexer::is_variable_name(array_name[i])) {
            is_plain_array_name = false;
            break;
          }
        if (is_plain_array_name) {
          let const is_star = segment_text[segment_text.length - 2] == '*';
          let const elements = collect_array_elements(array_name);
          if (segment.is_in_double_quotes && is_star) {
            let const ifs = m_field_separators.view();
            let joined = String{scratch_allocator()};
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
              joined.append(elements[i].view());
            }
            append_run(joined, false);
          } else if (segment.is_in_double_quotes) {
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0) flush();
              append_run(elements[i].view(), false);
            }
          } else {
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0) flush();
              append_split_run(elements[i].view(), true);
            }
          }
          break;
        }
      }
      /* "${@:off:len}" and "${*:off:len}" slice the positional parameters the
         way the array slice below slices elements, with index zero naming the
         shell itself the way bash counts $0 into the slice. The @ form keeps
         each parameter its own field, the * form joins them. */
      if ((segment_text[0] == '@' || segment_text[0] == '*') &&
          segment_text.length > 1 && segment_text[1] == ':')
      {
        let const is_star = segment_text[0] == '*';
        let const slice = segment_text.substring(2);
        let const param_count = m_positional_params.count();
        const i64 total = static_cast<i64>(param_count) + 1;
        auto positional_at = [&](i64 index) wontthrow -> StringView {
          return index == 0 ? m_shell_name.view()
                            : m_positional_params[static_cast<usize>(index - 1)]
                                  .view();
        };

        const usize sep = find_substring_length_separator(slice);
        const StringView offset_text = slice.substring_of_length(0, sep);
        const i64 offset =
            offset_text.is_empty() ? 0 : evaluate_arithmetic(offset_text);
        i64 start = offset < 0 ? total + offset : offset;
        if (start < 0) start = 0;
        if (start > total) start = total;
        i64 end = total;
        if (sep < slice.length) {
          const StringView length_text = slice.substring(sep + 1);
          const i64 length =
              length_text.is_empty() ? 0 : evaluate_arithmetic(length_text);
          if (length < 0)
            throw Error{"Unable to take the substring because the length names "
                        "a point before the offset"};
          end = start + length;
        }
        if (end > total) end = total;
        if (end < start) end = start;

        if (segment.is_in_double_quotes && is_star) {
          let const ifs = m_field_separators.view();
          let joined = String{scratch_allocator()};
          for (i64 j = start; j < end; j++) {
            if (j > start && !ifs.is_empty()) joined.push(ifs[0]);
            joined.append(positional_at(j));
          }
          append_run(joined, false);
        } else if (segment.is_in_double_quotes) {
          for (i64 j = start; j < end; j++) {
            if (j > start) flush();
            append_run(positional_at(j), false);
          }
        } else {
          for (i64 j = start; j < end; j++) {
            if (j > start) flush();
            append_split_run(positional_at(j), true);
          }
        }
        break;
      }
      /* "${a[@]:off:len}" and "${a[*]:off:len}" slice the element list, off
         naming the first element and len the count, with a negative off counted
         from the end. The @ form keeps each sliced element its own field, the *
         form joins them. */
      if (lexer::is_variable_name_start(segment_text[0])) {
        usize name_end = 1;
        while (name_end < segment_text.length &&
               lexer::is_variable_name(segment_text[name_end]))
          name_end++;
        if (name_end + 4 <= segment_text.length &&
            segment_text[name_end] == '[' &&
            (segment_text[name_end + 1] == '@' ||
             segment_text[name_end + 1] == '*') &&
            segment_text[name_end + 2] == ']' &&
            name_end + 3 < segment_text.length &&
            segment_text[name_end + 3] == ':')
        {
          let const array_name = segment_text.substring_of_length(0, name_end);
          let const is_star = segment_text[name_end + 1] == '*';
          let const slice = segment_text.substring(name_end + 4);
          let const elements = collect_array_elements(array_name);
          const i64 total = static_cast<i64>(elements.count());

          const usize sep = find_substring_length_separator(slice);
          const StringView offset_text = slice.substring_of_length(0, sep);
          const i64 offset =
              offset_text.is_empty() ? 0 : evaluate_arithmetic(offset_text);
          i64 start = offset < 0 ? total + offset : offset;
          if (start < 0) start = 0;
          if (start > total) start = total;
          i64 end = total;
          if (sep < slice.length) {
            const StringView length_text = slice.substring(sep + 1);
            const i64 length =
                length_text.is_empty() ? 0 : evaluate_arithmetic(length_text);
            /* Unlike a string substring, an array slice rejects a negative
               length the way bash does rather than counting from the end. */
            if (length < 0)
              throw Error{
                  "Unable to take the substring because the length names "
                  "a point before the offset"};
            end = start + length;
          }
          if (end > total) end = total;
          if (end < start) end = start;

          if (segment.is_in_double_quotes && is_star) {
            let const ifs = m_field_separators.view();
            let joined = String{scratch_allocator()};
            for (i64 j = start; j < end; j++) {
              if (j > start && !ifs.is_empty()) joined.push(ifs[0]);
              joined.append(elements[static_cast<usize>(j)].view());
            }
            append_run(joined, false);
          } else if (segment.is_in_double_quotes) {
            for (i64 j = start; j < end; j++) {
              if (j > start) flush();
              append_run(elements[static_cast<usize>(j)].view(), false);
            }
          } else {
            for (i64 j = start; j < end; j++) {
              if (j > start) flush();
              append_split_run(elements[static_cast<usize>(j)].view(), true);
            }
          }
          break;
        }
      }
      /* "${a[@]MOD}" maps a value-transform modifier over each element, one
         field per element the way "${a[@]}" does, while "${a[*]MOD}" joins the
         modified elements. The / replacement, the # and % trims, and the ^ and
         , case changes all map here, a different modifier falls through to the
         general scalar path. */
      if (lexer::is_variable_name_start(segment_text[0])) {
        usize name_end = 1;
        while (name_end < segment_text.length &&
               lexer::is_variable_name(segment_text[name_end]))
          name_end++;
        const char field_modifier_op = name_end + 3 < segment_text.length
                                           ? segment_text[name_end + 3]
                                           : '\0';
        if (name_end + 3 < segment_text.length &&
            segment_text[name_end] == '[' &&
            (segment_text[name_end + 1] == '@' ||
             segment_text[name_end + 1] == '*') &&
            segment_text[name_end + 2] == ']' &&
            (field_modifier_op == '/' || field_modifier_op == '#' ||
             field_modifier_op == '%' || field_modifier_op == '^' ||
             field_modifier_op == ','))
        {
          let const array_name = segment_text.substring_of_length(0, name_end);
          let const modifier = segment_text.substring(name_end + 3);
          let const is_star = segment_text[name_end + 1] == '*';
          let const elements = collect_array_elements(array_name);
          if (segment.is_in_double_quotes && is_star) {
            let const ifs = m_field_separators.view();
            let joined = String{scratch_allocator()};
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
              joined.append(
                  apply_value_modifier(elements[i].view(), modifier).view());
            }
            append_run(joined, false);
          } else {
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0) flush();
              let const modified =
                  apply_value_modifier(elements[i].view(), modifier);
              if (segment.is_in_double_quotes)
                append_run(modified.view(), false);
              else
                append_split_run(modified.view(), true);
            }
          }
          break;
        }
      }
      /* "${a[@]+word}" and "${a[@]-word}" pick between the word and the
         elements with field fidelity, the nounset-safe array expansion idiom
         bash-completion writes as ${a[@]+"${a[@]}"}. The dominant word shape,
         one quoted or bare array expansion, emits one field per element the
         way the plain "${a[@]}" does, and any other word shape falls through
         to the general scalar path below. */
      if (lexer::is_variable_name_start(segment_text[0])) {
        usize name_end = 1;
        while (name_end < segment_text.length &&
               lexer::is_variable_name(segment_text[name_end]))
          name_end++;
        if (name_end + 3 < segment_text.length &&
            segment_text[name_end] == '[' &&
            (segment_text[name_end + 1] == '@' ||
             segment_text[name_end + 1] == '*') &&
            segment_text[name_end + 2] == ']')
        {
          let const rest = segment_text.substring(name_end + 3);
          let const is_colon_form = !rest.is_empty() && rest[0] == ':';
          let const op_index = is_colon_form ? usize{1} : usize{0};
          if (op_index < rest.length &&
              (rest[op_index] == '+' || rest[op_index] == '-'))
          {
            let const array_name =
                segment_text.substring_of_length(0, name_end);
            let const modifier_op = rest[op_index];
            let const modifier_word = rest.substring(op_index + 1);
            let const is_star = segment_text[name_end + 1] == '*';
            let const elements = collect_array_elements(array_name);
            let is_every_element_empty = true;
            for (const String &element : elements)
              if (!element.is_empty()) {
                is_every_element_empty = false;
                break;
              }
            let const treat_as_unset =
                is_colon_form ? is_every_element_empty : elements.is_empty();
            let const should_expand_word =
                modifier_op == '+' ? !treat_as_unset : treat_as_unset;

            /* The per-element emitter the plain "${a[@]}" cases use, one
               field per element when quoted with @, an IFS join with *. The
               quoting follows the expanded text, so "${arr[@]}" stays one
               field per element even when the outer modifier is unquoted, the
               way bash keeps the inner quotes. */
            auto emit_elements = [&](const ArrayList<String> &values,
                                     bool quoted) throws {
              if (quoted && is_star) {
                let const ifs = m_field_separators.view();
                let joined = String{scratch_allocator()};
                for (usize i = 0; i < values.count(); i++) {
                  if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
                  joined.append(values[i].view());
                }
                append_run(joined, false);
                return;
              }
              for (usize i = 0; i < values.count(); i++) {
                if (i > 0) flush();
                if (quoted)
                  append_run(values[i].view(), false);
                else
                  append_split_run(values[i].view(), true);
              }
            };

            if (!should_expand_word) {
              /* + with an unset array contributes no field at all, and - with
                 a set array reads the elements themselves under the outer
                 quoting. */
              if (modifier_op == '-')
                emit_elements(elements, segment.is_in_double_quotes);
              break;
            }

            /* The word shape "${name[@]}" or its bare or starred forms, the
               only shapes the idiom uses, expands to that array's elements. */
            let word = modifier_word;
            let const is_word_quoted = word.length >= 2 && word[0] == '"' &&
                                       word[word.length - 1] == '"';
            if (is_word_quoted)
              word = word.substring_of_length(1, word.length - 2);
            if (word.length >= 6 && word[0] == '$' && word[1] == '{' &&
                word[word.length - 1] == '}')
            {
              let const inner = word.substring_of_length(2, word.length - 3);
              usize inner_name_end = 0;
              while (inner_name_end < inner.length &&
                     lexer::is_variable_name(inner[inner_name_end]))
                inner_name_end++;
              if (inner_name_end > 0 && inner_name_end + 3 == inner.length &&
                  inner[inner_name_end] == '[' &&
                  (inner[inner_name_end + 1] == '@' ||
                   inner[inner_name_end + 1] == '*') &&
                  inner[inner_name_end + 2] == ']')
              {
                /* The inner word's own quoting governs the split, so a quoted
                   "${arr[@]}" keeps each element whole even though the outer
                   modifier here is unquoted. */
                emit_elements(collect_array_elements(
                                  inner.substring_of_length(0, inner_name_end)),
                              is_word_quoted || segment.is_in_double_quotes);
                break;
              }
            }
            /* Any other word shape, a plain literal alternate such as
               ${a[@]+alt} or ${a[@]-default}, expands the modifier word
               itself rather than re-reading the whole a[@] segment, which the
               scalar path would mis-parse as ${a}. */
            let const value = expand_modifier_word(modifier_word);
            if (segment.is_in_double_quotes)
              append_run(value, false);
            else
              append_split_run(value, true);
            break;
          }
        }
      }
      /* "${name+word}" and "${name-word}" with a bare scalar subject keep the
         same field fidelity when the word is itself an array expansion. bash
         reads the bare name of an array as its element zero, and
         bash-completion writes ${words+"${words[@]}"}, where the general
         scalar path below would join the elements into one string and lose
         the empty ones on the re-split. A word of any other shape falls
         through to that path unchanged. */
      if (lexer::is_variable_name_start(segment_text[0])) {
        usize name_end = 1;
        while (name_end < segment_text.length &&
               lexer::is_variable_name(segment_text[name_end]))
          name_end++;
        let const rest = segment_text.substring(name_end);
        let const is_colon_form = !rest.is_empty() && rest[0] == ':';
        let const op_index = is_colon_form ? usize{1} : usize{0};
        if (op_index < rest.length &&
            (rest[op_index] == '+' || rest[op_index] == '-'))
        {
          let word = rest.substring(op_index + 1);
          let const is_word_quoted = word.length >= 2 && word[0] == '"' &&
                                     word[word.length - 1] == '"';
          if (is_word_quoted)
            word = word.substring_of_length(1, word.length - 2);
          if (word.length >= 6 && word[0] == '$' && word[1] == '{' &&
              word[word.length - 1] == '}')
          {
            let const inner = word.substring_of_length(2, word.length - 3);
            usize inner_name_end = 0;
            while (inner_name_end < inner.length &&
                   lexer::is_variable_name(inner[inner_name_end]))
              inner_name_end++;
            if (inner_name_end > 0 && inner_name_end + 3 == inner.length &&
                inner[inner_name_end] == '[' &&
                (inner[inner_name_end + 1] == '@' ||
                 inner[inner_name_end + 1] == '*') &&
                inner[inner_name_end + 2] == ']')
            {
              let const subject_elements = collect_array_elements(
                  segment_text.substring_of_length(0, name_end));
              /* The bare name reads as element zero, so the plain form tests
                 whether any element exists and the colon form whether the
                 first is nonempty. */
              let const treat_as_unset =
                  is_colon_form ? (subject_elements.is_empty() ||
                                   subject_elements[0].is_empty())
                                : subject_elements.is_empty();
              let const modifier_op = rest[op_index];
              let const should_expand_word =
                  modifier_op == '+' ? !treat_as_unset : treat_as_unset;
              if (should_expand_word) {
                let const values = collect_array_elements(
                    inner.substring_of_length(0, inner_name_end));
                let const emit_quoted =
                    is_word_quoted || segment.is_in_double_quotes;
                if (emit_quoted && inner[inner_name_end + 1] == '*') {
                  let const ifs = m_field_separators.view();
                  let joined = String{scratch_allocator()};
                  for (usize i = 0; i < values.count(); i++) {
                    if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
                    joined.append(values[i].view());
                  }
                  append_run(joined, false);
                } else {
                  for (usize i = 0; i < values.count(); i++) {
                    if (i > 0) flush();
                    if (emit_quoted)
                      append_run(values[i].view(), false);
                    else
                      append_split_run(values[i].view(), true);
                  }
                }
                break;
              }
              /* + with an unset subject contributes no field, while - with a
                 set subject reads the subject itself, which is the general
                 path's case below. */
              if (modifier_op == '+') break;
            }
          }
        }
      }
      /* A plain $name that names a set scalar appends the stored value by view,
         with no copy, since the full parameter expansion would read the same
         string. A spec with a modifier, an unset name, or a synthesized name is
         not found in the store and falls through to the general path. */
      if (!segment_text.is_empty() &&
          lexer::is_variable_name_start(segment_text[0]))
      {
        let is_plain_name = true;
        for (usize i = 1; i < segment_text.length; i++)
          if (!lexer::is_variable_name(segment_text[i])) {
            is_plain_name = false;
            break;
          }
        if (is_plain_name)
          if (let const *stored = lookup_shell_variable(segment_text)) {
            if (segment.is_in_double_quotes)
              append_run(stored->view(), false);
            else
              append_split_run(stored->view(), true);
            break;
          }
      }
      /* apply_parameter_expansion already returns an owned String, so it is
         bound directly rather than copied into a second allocation. */
      let const value = apply_parameter_expansion(segment.text.view());
      if (segment.is_in_double_quotes)
        append_run(value, false);
      else
        /* An unquoted expansion undergoes field splitting and then pathname
           expansion, so a * or ? from the value is an active glob. */
        append_split_run(value, true);
    } break;

    case WordSegment::Kind::CommandSubstitution: {
      let const output = capture_command_substitution(segment);
      if (segment.is_in_double_quotes)
        append_run(output, false);
      else
        append_split_run(output, true);
    } break;

    case WordSegment::Kind::ProcessSubstitution: {
      /* The /dev/fd path is a single literal field, so it neither splits on IFS
         nor globs, the way bash substitutes the process substitution. */
      let const path = setup_process_substitution(segment.text.view());
      append_run(path, false);
    } break;

    case WordSegment::Kind::ArithmeticExpansion: {
      /* A constant arithmetic segment was folded at analyze time, so the result
         is read straight from the cache rather than re-parsed here. */
      let const result = segment.folded_arithmetic_result.has_value()
                             ? *segment.folded_arithmetic_result
                             : evaluate_arithmetic(segment.text.view());
      /* The field copies the digits in, so the conversion writes into a stack
         buffer and appends a view, with no heap allocation. */
      char buffer[24];
      let const value = utils::int_to_text_into(result, buffer, sizeof(buffer));
      if (segment.is_in_double_quotes)
        append_run(value, false);
      else
        append_split_run(value, false);
    } break;
    }
  }

  flush();

  return fields;
}

hot fn EvalContext::expand_word_for_assignment(const Word &word) throws
    -> String
{
  LOG(verbosity::All, "expanding an assignment word of %zu segments",
      word.segments.count());
  /* Only copy the segments when a tilde must be rewritten, the leading one or
     one after an unquoted colon, the assignment-only rule bash applies to
     PATH=~/bin:~/tmp. The common assignment reads its segments in place with
     no per-command copy. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{scratch_allocator()};
  let const has_leading_tilde =
      !word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~';
  let has_colon_tilde = false;
  for (const WordSegment &segment : word.segments) {
    if (!segment.is_tilde_candidate()) continue;
    let const view = segment.text.view();
    for (usize i = 0; i + 1 < view.length; i++)
      if (view[i] == ':' && view[i + 1] == '~') {
        has_colon_tilde = true;
        break;
      }
    if (has_colon_tilde) break;
  }
  if (has_leading_tilde || has_colon_tilde) {
    tilde_expanded_segments = word.segments;
    if (has_leading_tilde)
      expand_tilde(tilde_expanded_segments.front(),
                   tilde_expanded_segments.count() > 1);
    if (has_colon_tilde)
      for (usize i = 0; i < tilde_expanded_segments.count(); i++)
        expand_colon_tildes(tilde_expanded_segments[i],
                            i + 1 < tilde_expanded_segments.count());
    segments = &tilde_expanded_segments;
  }

  let result = String{heap_allocator()};
  for (const WordSegment &segment : *segments) {
    let const segment_text = segment.text.view();
    switch (segment.kind) {
    case WordSegment::Kind::VariableReference:
      result += apply_parameter_expansion(segment_text);
      break;
    case WordSegment::Kind::CommandSubstitution:
      result += capture_command_substitution(segment);
      break;
    case WordSegment::Kind::ArithmeticExpansion:
      result += utils::int_to_text(segment.folded_arithmetic_result.has_value()
                                       ? *segment.folded_arithmetic_result
                                       : evaluate_arithmetic(segment_text));
      break;
    default: result += segment_text; break;
    }
  }
  return result;
}

fn EvalContext::expand_case_pattern_masked(const Word &word,
                                           ArrayList<bool> &active_out) throws
    -> String
{
  /* Only copy the segments when a leading tilde must be rewritten, mirroring
     the assignment expansion the case word otherwise shares. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{scratch_allocator()};
  if (!word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front(),
                 tilde_expanded_segments.count() > 1);
    segments = &tilde_expanded_segments;
  }

  let result = String{heap_allocator()};

  /* Append a run of bytes that share one glob-active state, so the mask stays
     parallel to the result the way expand_word builds it. */
  auto emit_run = [&](StringView bytes, bool is_active) {
    result.append(bytes);
    for (usize k = 0; k < bytes.length; k++)
      active_out.push(is_active);
  };

  for (const WordSegment &segment : *segments) {
    let const segment_text = segment.text.view();
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText:
    case WordSegment::Kind::DoubleQuotedText:
      /* A quoted or double-quoted region is a literal member, so its
         metacharacters never act as wildcards. */
      emit_run(segment_text, false);
      break;
    case WordSegment::Kind::UnquotedText: emit_run(segment_text, true); break;
    case WordSegment::Kind::VariableReference: {
      let const value = apply_parameter_expansion(segment_text);
      emit_run(value.view(), !segment.is_in_double_quotes);
    } break;
    case WordSegment::Kind::CommandSubstitution: {
      let const output = capture_command_substitution(segment);
      emit_run(output.view(), !segment.is_in_double_quotes);
    } break;
    case WordSegment::Kind::ProcessSubstitution: {
      /* The /dev/fd path is a literal that does not glob. */
      let const path = setup_process_substitution(segment.text.view());
      emit_run(path.view(), false);
    } break;
    case WordSegment::Kind::ArithmeticExpansion: {
      /* An arithmetic result is decimal digits and a sign, so it carries no
         glob metacharacter and stays inactive. */
      let const number = segment.folded_arithmetic_result.has_value()
                             ? *segment.folded_arithmetic_result
                             : evaluate_arithmetic(segment_text);
      emit_run(utils::int_to_text(number).view(), false);
    } break;
    }
  }
  return result;
}

fn EvalContext::expand_wordlist_to_fields(StringView wordlist,
                                          bool allow_expansion) throws
    -> ArrayList<String>
{
  auto split_plain = [&]() throws -> ArrayList<String> {
    let words = ArrayList<String>{};
    usize start = 0;
    for (usize i = 0; i <= wordlist.length; i++) {
      const char c = i < wordlist.length ? wordlist[i] : ' ';
      if (c == ' ' || c == '\t' || c == '\n') {
        if (i > start)
          words.push(String{wordlist.substring_of_length(start, i - start)});
        start = i + 1;
      }
    }
    return words;
  };

  /* The ghost path never parses, so it skips even the metacharacter scan. */
  if (!allow_expansion) return split_plain();

  /* A literal list, no expansion or quoting byte anywhere, splits with no
     parse at all, the common -W shape. */
  let needs_expansion = false;
  for (usize i = 0; i < wordlist.length && !needs_expansion; i++) {
    const char c = wordlist[i];
    needs_expansion = c == '$' || c == '`' || c == '"' || c == '\'' ||
                      c == '\\' || c == '~' || c == '{';
  }
  if (!needs_expansion) return split_plain();

  /* The list expands by wrapping it in an array literal, so a structural byte
     that would close the literal early and run the rest as a command, a
     top-level ')' or ';' or '|' or '&' or '(' or a comment '#', is a break-out
     a malicious or careless -W list could carry. Such a list degrades to the
     plain split rather than executing the tail. The scan tracks quotes and the
     $(...) and ${...} and backtick nesting so a paren inside an expansion is
     not mistaken for a top-level one. */
  auto is_array_literal_safe = [&]() wontthrow -> bool {
    char quote = 0;
    usize paren_depth = 0;
    usize brace_depth = 0;
    bool in_backtick = false;
    bool at_word_start = true;
    for (usize i = 0; i < wordlist.length; i++) {
      const char c = wordlist[i];
      if (quote != 0) {
        if (c == quote) quote = 0;
        at_word_start = false;
        continue;
      }
      if (c == '\\') {
        i++;
        at_word_start = false;
        continue;
      }
      if (c == '\'' || c == '"') {
        quote = c;
      } else if (c == '`') {
        in_backtick = !in_backtick;
      } else if (c == '$' && i + 1 < wordlist.length && wordlist[i + 1] == '(')
      {
        /* $(( opens arithmetic that closes with )), so both parens are
           counted and the )) decrements back to the top level cleanly. */
        if (i + 2 < wordlist.length && wordlist[i + 2] == '(') {
          paren_depth += 2;
          i += 2;
        } else {
          paren_depth++;
          i++;
        }
      } else if (c == '$' && i + 1 < wordlist.length && wordlist[i + 1] == '{')
      {
        brace_depth++;
        i++;
      } else if (c == ')' && paren_depth > 0) {
        paren_depth--;
      } else if (c == '}' && brace_depth > 0) {
        brace_depth--;
      } else if (!in_backtick && paren_depth == 0 && brace_depth == 0) {
        if (c == ')' || c == '(' || c == ';' || c == '|' || c == '&' ||
            c == '<' || c == '>' || c == '\n')
          return false;
        if (c == '#' && at_word_start) return false;
      }
      at_word_start = c == ' ' || c == '\t';
    }
    return quote == 0 && !in_backtick && paren_depth == 0 && brace_depth == 0;
  };
  if (!is_array_literal_safe()) {
    LOG(verbosity::Debug, "-W list is not array-literal safe, splitting plain");
    return split_plain();
  }

  /* The list expands as an array literal in the current context, so a word
     such as "${options[@]}" reaches the caller's array. The defer drops the
     temp name on the success and the failure path alike. */
  defer
  {
    m_indexed_arrays.erase("t__wordlist_fields");
    force_unset_shell_variable("t__wordlist_fields");
  };
  let fields = ArrayList<String>{};
  try {
    let expansion_source = String{"t__wordlist_fields=("};
    expansion_source.append(wordlist);
    expansion_source.push(')');
    run_source(expansion_source.view(), "a -W word list", false);
    if (const ArrayList<String> *expanded =
            lookup_indexed_array("t__wordlist_fields");
        expanded != nullptr)
    {
      fields.reserve(expanded->count());
      for (const String &word : *expanded)
        fields.push_managed(word.view());
    }
  } catch (const ErrorBase &error) {
    LOG(verbosity::Debug, "-W expansion failed, splitting plain: %s",
        error.message().c_str());
    return split_plain();
  }
  return fields;
}

} /* namespace shit */
