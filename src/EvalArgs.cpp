#include "Arena.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "ResolvedCommand.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

namespace {

/* The byte standing in for an opaque segment in the brace-expansion template, a
   quoted run or variable reference whose braces and commas must not act as
   brace structure. It is followed by the segment's index. */
constexpr char BRACE_OPAQUE_MARKER = '\x01';

/* True when a word carries a { in an unquoted segment, the only place brace
   structure can appear. The common brace-free word skips the expansion. */
pure fn word_has_brace_candidate(const Word &word) wontthrow -> bool
{
  for (const WordSegment &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::UnquotedText) continue;
    for (usize i = 0; i < segment.text.count(); i++)
      if (segment.text[i] == '{') return true;
  }
  return false;
}

/* A signed integer with the width of its digit run, so {01..10} can pad its
   output to the wider operand. */
struct sequence_integer
{
  i64 value;
  usize digit_width;
  bool has_leading_zero;
};

fn parse_sequence_integer(StringView text) wontthrow -> Maybe<sequence_integer>
{
  if (text.is_empty()) return None;
  usize i = 0;
  if (text[0] == '-' || text[0] == '+') {
    i++;
  }
  const usize digit_start = i;
  for (; i < text.length; i++)
    if (text[i] < '0' || text[i] > '9') return None;
  if (i == digit_start) return None;

  i64 magnitude = 0;
  for (usize j = digit_start; j < text.length; j++) {
    const i64 digit = text[j] - '0';
    /* A bound past the signed range leaves the brace literal rather than
       overflowing during the parse. */
    if (magnitude > (9223372036854775807LL - digit) / 10) return None;
    magnitude = magnitude * 10 + digit;
  }
  const i64 value = text[0] == '-' ? -magnitude : magnitude;
  const usize width = text.length - digit_start;
  const bool leading_zero = width > 1 && text[digit_start] == '0';
  return sequence_integer{value, width, leading_zero};
}

/* Split a sequence body on its .. separators into start, end, and optional
   step. */
fn split_sequence_parts(StringView content, Allocator alloc) throws
    -> ArrayList<StringView>
{
  let parts = ArrayList<StringView>{alloc};
  usize start = 0;
  usize i = 0;
  while (i + 1 < content.length) {
    if (content[i] == '.' && content[i + 1] == '.') {
      parts.push(content.substring_of_length(start, i - start));
      i += 2;
      start = i;
      continue;
    }
    i++;
  }
  parts.push(content.substring(start));
  return parts;
}

/* The elements of a {start..end} or {start..end..step} sequence, numeric or
   single-letter, or None when the body is not a sequence. */
fn parse_brace_sequence(StringView content, Allocator alloc) throws
    -> Maybe<ArrayList<String>>
{
  let const parts = split_sequence_parts(content, alloc);
  if (parts.count() != 2 && parts.count() != 3) return None;

  i64 step = 1;
  if (parts.count() == 3) {
    let const parsed_step = parse_sequence_integer(parts[2]);
    if (!parsed_step.has_value()) return None;
    step = parsed_step->value;
  }
  if (step == 0) step = 1;
  const i64 magnitude = step < 0 ? -step : step;

  let const start_int = parse_sequence_integer(parts[0]);
  let const end_int = parse_sequence_integer(parts[1]);
  if (start_int.has_value() && end_int.has_value()) {
    const i64 from = start_int->value;
    const i64 to = end_int->value;
    const i64 increment = from <= to ? magnitude : -magnitude;
    const bool pad = start_int->has_leading_zero || end_int->has_leading_zero;
    const usize width = pad ? (start_int->digit_width > end_int->digit_width
                                   ? start_int->digit_width
                                   : end_int->digit_width)
                            : 0;
    let elements = ArrayList<String>{alloc};
    for (i64 v = from; increment > 0 ? v <= to : v >= to; v += increment) {
      String number = utils::int_to_text(v);
      if (pad) {
        const bool negative = !number.is_empty() && number.view()[0] == '-';
        const StringView digits = number.view().substring(negative ? 1 : 0);
        if (digits.length < width) {
          let padded = String{alloc};
          if (negative) padded.push('-');
          for (usize z = digits.length; z < width; z++)
            padded.push('0');
          padded.append(digits);
          number = steal(padded);
        }
      }
      elements.push(steal(number));
    }
    return elements;
  }

  if (parts[0].length == 1 && parts[1].length == 1) {
    const char from = parts[0][0];
    const char to = parts[1][0];
    const bool from_alpha =
        (from >= 'a' && from <= 'z') || (from >= 'A' && from <= 'Z');
    const bool to_alpha = (to >= 'a' && to <= 'z') || (to >= 'A' && to <= 'Z');
    if (from_alpha && to_alpha) {
      const i64 increment = from <= to ? magnitude : -magnitude;
      let elements = ArrayList<String>{alloc};
      for (i64 c = from; increment > 0 ? c <= to : c >= to; c += increment) {
        let element = String{alloc};
        element.push(static_cast<char>(c));
        elements.push(steal(element));
      }
      return elements;
    }
  }
  return None;
}

/* The alternatives of a brace group body, or None when the braces are
   literal. */
fn brace_group_alternatives(StringView content, Allocator alloc) throws
    -> Maybe<ArrayList<String>>
{
  usize depth = 0;
  let comma_positions = ArrayList<usize>{alloc};
  for (usize i = 0; i < content.length; i++) {
    const char c = content[i];
    if (c == '{') {
      depth++;
    } else if (c == '}') {
      if (depth > 0) depth--;
    } else if (c == ',' && depth == 0) {
      comma_positions.push(i);
    }
  }

  if (!comma_positions.is_empty()) {
    let alternatives = ArrayList<String>{alloc};
    usize start = 0;
    for (const usize comma : comma_positions) {
      alternatives.push(
          String{alloc, content.substring_of_length(start, comma - start)});
      start = comma + 1;
    }
    alternatives.push_managed(content.substring(start));
    return alternatives;
  }

  return parse_brace_sequence(content, alloc);
}

struct brace_group
{
  usize open;
  usize close;
  ArrayList<String> alternatives{heap_allocator()};
};

fn find_brace_group(StringView text, Allocator alloc) throws
    -> Maybe<brace_group>
{
  for (usize open = 0; open < text.length; open++) {
    if (text[open] != '{') continue;
    usize depth = 0;
    for (usize j = open; j < text.length; j++) {
      const char c = text[j];
      if (c == '{') {
        depth++;
      } else if (c == '}') {
        depth--;
        if (depth == 0) {
          let alternatives = brace_group_alternatives(
              text.substring_of_length(open + 1, j - open - 1), alloc);
          if (alternatives.has_value()) {
            let group = brace_group{open, j, {}};
            group.alternatives = steal(*alternatives);
            return group;
          }
          break;
        }
      }
    }
  }
  return None;
}

/* The deepest brace nesting expanded before the recursion is cut, so a
   pathological input such as {a,{b,{c,...}}} cannot overflow the native stack.
 */
constexpr usize MAX_BRACE_DEPTH = 256;

/* Expand the brace structure in a template string into the cartesian product,
   leaving any opaque marker untouched. A nesting past the cap leaves the
   remaining text literal rather than recursing further. */
fn brace_expand_text(StringView text, Allocator alloc, usize depth = 0) throws
    -> ArrayList<String>
{
  let results = ArrayList<String>{alloc};
  let const group = find_brace_group(text, alloc);
  if (!group.has_value() || depth >= MAX_BRACE_DEPTH) {
    results.push_managed(text);
    return results;
  }

  const StringView preamble = text.substring_of_length(0, group->open);
  const StringView postamble = text.substring(group->close + 1);
  let const post_expansions = brace_expand_text(postamble, alloc, depth + 1);

  for (const String &alternative : group->alternatives) {
    for (const String &expanded_alt :
         brace_expand_text(alternative.view(), alloc, depth + 1))
    {
      for (const String &expanded_post : post_expansions) {
        let combined = String{alloc, preamble};
        combined.append(expanded_alt.view());
        combined.append(expanded_post.view());
        results.push(steal(combined));
      }
    }
  }
  return results;
}

/* Expand the brace structure of a word into the words it spells. Every
   non-unquoted segment is recorded as an opaque marker, so a quoted brace or a
   variable stays intact. */
fn expand_braces(const Word &word, Allocator alloc) throws -> ArrayList<Word>
{
  let opaque_segments = ArrayList<const WordSegment *>{alloc};
  let word_template = String{alloc};
  for (const WordSegment &segment : word.segments) {
    if (segment.kind == WordSegment::Kind::UnquotedText) {
      word_template.append(segment.text.view());
    } else {
      ASSERT(opaque_segments.count() < 256);
      word_template.push(BRACE_OPAQUE_MARKER);
      word_template.push(static_cast<char>(opaque_segments.count()));
      opaque_segments.push(&segment);
    }
  }

  let const expanded = brace_expand_text(word_template.view(), alloc);

  let words = ArrayList<Word>{alloc};
  for (const String &produced : expanded) {
    let out = Word{};
    let run = String{alloc};
    for (usize i = 0; i < produced.count(); i++) {
      const char c = produced[i];
      /* A literal 0x01 byte from $'\x01' is not a marker, so the index bound is
         checked at run time and a failed check copies the byte verbatim rather
         than reading past the segment list. */
      const bool is_opaque_marker =
          c == BRACE_OPAQUE_MARKER && i + 1 < produced.count() &&
          static_cast<u8>(produced[i + 1]) < opaque_segments.count();
      if (is_opaque_marker) {
        if (!run.is_empty()) {
          out.segments.push(
              WordSegment{WordSegment::Kind::UnquotedText, steal(run), false});
          run = String{alloc};
        }
        const u8 index = static_cast<u8>(produced[++i]);
        out.segments.push(*opaque_segments[index]);
      } else {
        run.push(c);
      }
    }
    if (!run.is_empty()) {
      out.segments.push(
          WordSegment{WordSegment::Kind::UnquotedText, steal(run), false});
    }
    words.push(steal(out));
  }
  LOG(Debug, "brace expansion produced %zu words", words.count());
  return words;
}

} // namespace

hot fn EvalContext::process_args(const ArrayList<const Token *> &args,
                                 bool args_are_transient) throws
    -> ArrayList<String>
{
  LOG(Debug, "expanding %zu argument tokens", args.count());
  /* The vector lives on the scratch arena for a transient request the caller
     scopes and frees, or on the heap otherwise. The per-word fields are
     reclaimed on return only for the heap form, since the transient form leaves
     them on the caller's scratch region. The mark nests, so a command
     substitution inside one of these words reclaims only its own fields. */
  let expanded_args = args_are_transient
                          ? ArrayList<String>{scratch_allocator()}
                          : ArrayList<String>{};
  expanded_args.reserve(args.count());

  let const fields_mark = m_scratch_arena.mark();
  defer
  {
    if (!args_are_transient) m_scratch_arena.release(fields_mark);
  };

  /* A declaration builtin, such as local or export, treats a name=value
     argument as an assignment whose value expands with no field splitting or
     globbing. Only the plain literal command word is decided this way, before
     any expansion, the way bash does. */
  let is_declaration_command = false;
  let is_local_command = false;
  let is_declare_command = false;
  let is_test_command = false;
  if (!args.is_empty() && args[0]->kind() == Token::Kind::Word) {
    const Word &command_word =
        static_cast<const tokens::WordToken *>(args[0])->word();
    if (command_word.plain_literal_kind() != Word::PlainLiteral::NotPlain) {
      /* The joined copy is built only for the rare split word. */
      let joined_name = String{scratch_allocator()};
      StringView name;
      if (command_word.segments.count() == 1) {
        name = command_word.segments[0].text.view();
      } else {
        for (const WordSegment &segment : command_word.segments)
          joined_name.append(segment.text.view());
        name = joined_name.view();
      }
      is_local_command = name == "local";
      is_declare_command = name == "declare" || name == "typeset";
      is_declaration_command = is_local_command || is_declare_command ||
                               name == "export" || name == "readonly";
      is_test_command = name == "test";
    }
    /* The lone bracket is the test builtin as a command word and earns the same
       glob exemption, though it never classifies as a plain literal above. */
    else if (command_word.segments.count() == 1 &&
             command_word.segments[0].kind == WordSegment::Kind::UnquotedText &&
             command_word.segments[0].text.view() == "[")
    {
      is_test_command = true;
    }
  }

  /* A test or [ command probes the filesystem, so an unmatched glob there stays
     literal in silence and the probe returns false rather than tripping
     failglob. A user function named test keeps the exemption. */
  let const previous_glob_exempt = m_glob_exempt_for_test;
  m_glob_exempt_for_test = is_test_command;
  defer { m_glob_exempt_for_test = previous_glob_exempt; };

  /* An unset variable in a test operand is the question the command asks, so
     the unset warning is suppressed across the operand expansion. An explicit
     set -u still aborts, so this silences only the advisory warning. */
  let const previous_suppress_test_warning =
      is_warning_suppressed(suppressible_warning::UnsetTestOperand);
  if (is_test_command)
    set_warning_suppressed(suppressible_warning::UnsetTestOperand, true);
  defer
  {
    set_warning_suppressed(suppressible_warning::UnsetTestOperand,
                           previous_suppress_test_warning);
  };

  for (const Token *token : args) {
    let const location = token->source_location();
    try {
      /* Any non-word token is wrapped as one unquoted literal word. */
      let fallback_word = Word{};
      const Word *word = nullptr;
      if (token->kind() == Token::Kind::Word) {
        word = &static_cast<const tokens::WordToken *>(token)->word();
      } else if (token->kind() == Token::Kind::Assignment) {
        let const assignment_token =
            static_cast<const tokens::Assignment *>(token);
        ASSERT(assignment_token != nullptr);
        if (is_declaration_command) {
          /* A declaration builtin's name=value value expands with no splitting.
           */
          let assignment = String{expanded_args.allocator()};
          assignment.append(assignment_token->key().view());
          if (assignment_token->is_append() &&
              (is_local_command || is_declare_command))
          {
            /* local shadows an outer name and declare may apply -i on the same
               command, so the name+=value form passes through literally and the
               builtin computes the append after its own effects exist. */
            assignment += '+';
            assignment += '=';
            assignment.append(
                expand_word_for_assignment(assignment_token->value_word())
                    .view());
          } else {
            assignment += '=';
            /* The append form name+=value concatenates onto the name's current
               value. export and readonly do not shadow, so the current value is
               read here correctly. */
            if (assignment_token->is_append())
              assignment.append(get_variable_value(assignment_token->key())
                                    .value_or(String{})
                                    .view());
            let const expanded_value =
                expand_word_for_assignment(assignment_token->value_word());
            /* An integer name adds rather than concatenates. */
            if (assignment_token->is_append() &&
                is_integer_variable(assignment_token->key()))
              append_integer_expression(assignment, expanded_value.view());
            else
              assignment.append(expanded_value.view());
          }
          expanded_args.push(steal(assignment));
          continue;
        }
        /* An assignment as an argument, like echo k=$v, is an ordinary word
           rebuilt so the value still expands. */
        let key_literal = String{StringView{assignment_token->key()}};
        if (assignment_token->is_append()) key_literal += "+";
        key_literal += "=";
        fallback_word.segments.push(WordSegment{WordSegment::Kind::LiteralText,
                                                steal(key_literal), false});
        let const &value = assignment_token->value_word();
        for (const WordSegment &value_segment : value.segments)
          fallback_word.segments.push(value_segment);
        word = &fallback_word;
      } else {
        fallback_word.segments.push(WordSegment{WordSegment::Kind::UnquotedText,
                                                token->raw_string(), false});
        word = &fallback_word;
      }

      /* The plain-literal fast path pushes a word that needs no expansion,
         splitting, or globbing straight to the argument vector. */
      let const do_expand_one_word = [&](const Word &expandable)
                                         throws -> void {
        let const plain_kind = expandable.plain_literal_kind();
        let did_take_fast_path = false;
        if (plain_kind != Word::PlainLiteral::NotPlain) {
          let literal = String{expanded_args.allocator()};
          for (const WordSegment &segment : expandable.segments)
            literal.append(segment.text.view());

          /* A single unquoted segment still needs the IFS check, since an IFS
             byte in its text would split it into more than one field. */
          let should_split = false;
          if (plain_kind == Word::PlainLiteral::PlainUnquotedOneSegment) {
            for (usize i = 0; i < literal.count(); i++)
              if (is_field_separator(literal[i])) {
                should_split = true;
                break;
              }
          }

          if (!should_split) {
            expanded_args.push(steal(literal));
            did_take_fast_path = true;
          }
        }

        /* The single-field fast path covers a word that can neither split nor
           glob, so it expands straight into one argument with no glob_field, no
           directory scan, and no copy through expand_path. A quoted variable,
           a literal run, a double-quoted run, and an arithmetic result all
           qualify. A positional or array reference, an unquoted segment, a
           substitution, and a leading tilde fall through to the full machine. */
        if (!did_take_fast_path) {
          let is_single_field = !expandable.segments.is_empty();
          for (const WordSegment &segment : expandable.segments) {
            if (!is_single_field) break;

            if (segment.is_tilde_candidate() && !segment.text.is_empty() &&
                segment.text.first_character() == '~')
            {
              is_single_field = false;
              break;
            }

            switch (segment.kind) {
            case WordSegment::Kind::LiteralText:
            case WordSegment::Kind::DoubleQuotedText:
            case WordSegment::Kind::ArithmeticExpansion: break;
            case WordSegment::Kind::VariableReference: {
              /* An unquoted reference splits, and a reference naming a
                 positional list or an array element expands to many fields. The
                 '@', '*', and '[' bytes mark exactly those, so their absence
                 leaves a scalar that stays one field. The spec is walked once
                 for the three markers rather than three times. */
              let const spec = segment.text.view();
              let has_multi_field_marker = !segment.is_in_double_quotes;
              for (usize i = 0; !has_multi_field_marker && i < spec.length; i++) {
                let const byte = spec[i];
                if (byte == '@' || byte == '*' || byte == '[') {
                  has_multi_field_marker = true;
                }
              }
              if (has_multi_field_marker) is_single_field = false;
            } break;
            default: is_single_field = false; break;
            }
          }

          if (is_single_field) {
            let value = String{expanded_args.allocator()};
            for (const WordSegment &segment : expandable.segments) {
              switch (segment.kind) {
              case WordSegment::Kind::VariableReference: {
                /* A plain set scalar reads straight from the store into the
                   field, skipping the value copy a general expansion returns.
                   An unset, special, or modified reference still runs the full
                   parameter expansion. */
                let const spec = segment.text.view();
                let is_plain_name =
                    !spec.is_empty() && lexer::is_variable_name_start(spec[0]);
                for (usize i = 1; is_plain_name && i < spec.length; i++)
                  if (!lexer::is_variable_name(spec[i])) is_plain_name = false;
                if (is_plain_name)
                  if (let const *stored = lookup_shell_variable(spec)) {
                    value += stored->view();
                    break;
                  }
                value += apply_parameter_expansion(spec);
              } break;
              case WordSegment::Kind::ArithmeticExpansion: {
                let const number = segment.folded_arithmetic_result.has_value()
                                       ? *segment.folded_arithmetic_result
                                       : evaluate_arithmetic_cached(segment);
                char buffer[24];
                value += utils::int_to_text_into(number, buffer, sizeof(buffer));
              } break;
              default: value += segment.text.view(); break;
              }
            }
            expanded_args.push(steal(value));
            did_take_fast_path = true;
          }
        }

        if (!did_take_fast_path) {
          for (glob_field &field : expand_word(expandable)) {
            for (String &g : expand_path(steal(field), location))
              expanded_args.push_managed(StringView{g.c_str(), g.count()});
          }
        }
      };

      /* Brace expansion runs first in every mood but POSIX. */
      if (bash_additions_enabled() && word_has_brace_candidate(*word)) {
        for (const Word &brace_word : expand_braces(*word, scratch_allocator()))
          do_expand_one_word(brace_word);
      } else {
        do_expand_one_word(*word);
      }
    } catch (const Error &e) {
      throw relocate_error(e, location);
    }
  }

  /* The trace goes to standard error so it stays out of a command
     substitution's captured output. The plus is repeated once per enclosing
     subshell, so the top shell shows '+', a substitution '++'. */
  if (should_echo_expanded()) {
    let trace = String{};
    for (usize i = 0; i < m_subshell_depth + 1; i++)
      trace.push('+');
    trace.push(' ');
    trace.append(utils::merge_args_to_string(expanded_args));
    trace.push('\n');
    shit::print_error(trace);
  }

  return expanded_args;
}

} // namespace shit
