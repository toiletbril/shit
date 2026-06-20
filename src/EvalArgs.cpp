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

/* The byte that stands in for an opaque segment in the brace-expansion
   template, a quoted run or a variable reference whose braces and commas must
   not act as brace structure. It is followed by the segment's index. */
constexpr char BRACE_OPAQUE_MARKER = '\x01';

/* True when a word carries a { in an unquoted segment, the only place brace
   structure can appear. The scan is cheap so the common brace-free word skips
   the expansion entirely. */
pure fn word_has_brace_candidate(const Word &word) wontthrow -> bool
{
  for (const WordSegment &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::UnquotedText) continue;
    for (usize i = 0; i < segment.text.count(); i++)
      if (segment.text[i] == '{') return true;
  }
  return false;
}

/* Parse a signed integer and report the width of its digit run, so a sequence
   such as {01..10} can pad its output to the wider operand. None when the text
   is not a plain optionally-signed integer. */
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
    /* A bound past the signed range is not a usable sequence, so the brace is
       left literal rather than overflowing during the parse. */
    if (magnitude > (9223372036854775807LL - digit) / 10) return None;
    magnitude = magnitude * 10 + digit;
  }
  const i64 value = text[0] == '-' ? -magnitude : magnitude;
  const usize width = text.length - digit_start;
  const bool leading_zero = width > 1 && text[digit_start] == '0';
  return sequence_integer{value, width, leading_zero};
}

/* Split a sequence body on its .. separators into two or three parts, the
   start, the end, and an optional step. */
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

  /* A single-letter range counts through the alphabet. */
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

/* The alternatives of a brace group body, the comma-separated list or the
   sequence it spells, or None when the body is neither and the braces are
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
   The cap matches the globstar one for one consistent bound on user-driven
   recursion. */
constexpr usize MAX_BRACE_DEPTH = 256;

/* Expand the brace structure in a template string, leaving any opaque marker
   untouched. The recursion handles a nested group inside an alternative and a
   further group after the close, so the result is the cartesian product. A
   nesting past the cap leaves the remaining text literal rather than recursing
   further, the way a runaway expansion is bounded instead of crashing. */
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

/* Expand the brace structure of a word into the words it spells. The unquoted
   segments contribute their text to a template while every other segment is
   recorded as an opaque marker, so a quoted brace or a variable stays intact.
   Each expanded template is rebuilt into a word. */
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
      /* The marker is followed by an in-range segment index only when this
         scanner inserted it. A literal 0x01 byte that reached the text from
         $'\x01' is not a marker, so the index bound is checked at run time and
         a failed check copies the byte verbatim rather than reading past the
         segment list. */
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

} /* namespace */

hot fn EvalContext::process_args(const ArrayList<const Token *> &args,
                                 bool args_are_transient) throws
    -> ArrayList<String>
{
  LOG(Debug, "expanding %zu argument tokens", args.count());
  /* The argument vector is built first, on the scratch arena for a transient
     request the caller scopes and frees, or on the heap otherwise. The per-word
     expansion fields are reclaimed on return only for the heap form, since the
     transient form leaves the fields on the caller's scratch region to be freed
     with the vector after the command. The mark nests, so a command
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
     argument as an assignment, so its value expands with no field splitting or
     globbing. The command word is the first argument, and only its plain
     literal form is treated this way, the same form bash decides on before any
     expansion. */
  let is_declaration_command = false;
  let is_local_command = false;
  let is_declare_command = false;
  let is_test_command = false;
  if (!args.is_empty() && args[0]->kind() == Token::Kind::Word) {
    const Word &command_word =
        static_cast<const tokens::WordToken *>(args[0])->word();
    if (command_word.plain_literal_kind() != Word::PlainLiteral::NotPlain) {
      /* Nearly every command word is one literal segment, so its view serves
         directly and the joined copy is built only for the rare split word. */
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
    /* The lone bracket carries a glob metacharacter, so it never classifies as
       a plain literal above, while as a command word it is the test builtin
       and earns the same glob exemption. */
    else if (command_word.segments.count() == 1 &&
             command_word.segments[0].kind == WordSegment::Kind::UnquotedText &&
             command_word.segments[0].text.view() == "[")
    {
      is_test_command = true;
    }
  }

  /* A test or [ command reads its arguments to probe the filesystem, so an
     unmatched glob there stays literal in silence and the probe returns false
     naturally, rather than tripping failglob on the check that asks whether a
     file exists. A user function named test keeps the exemption, the cost of
     deciding before expansion. */
  let const previous_glob_exempt = m_glob_exempt_for_test;
  m_glob_exempt_for_test = is_test_command;
  defer { m_glob_exempt_for_test = previous_glob_exempt; };

  /* A test or [ command reads an operand to probe presence or emptiness, so an
     unset variable there is the question the command asks rather than a
     mistake. The unset warning is suppressed across the operand expansion the
     way test -z "$x" reads, matching the glob exemption above. An explicit set
     -u still aborts, so this silences only the advisory warning. */
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
      /* A word token is expanded in place. Any other token is wrapped as one
         unquoted literal word, which is the only case that needs a temporary.
       */
      let fallback_word = Word{};
      const Word *word = nullptr;
      if (token->kind() == Token::Kind::Word) {
        word = &static_cast<const tokens::WordToken *>(token)->word();
      } else if (token->kind() == Token::Kind::Assignment) {
        let const assignment_token =
            static_cast<const tokens::Assignment *>(token);
        ASSERT(assignment_token != nullptr);
        if (is_declaration_command) {
          /* A declaration builtin treats name=value as an assignment, so the
             value expands with no field splitting or globbing, the way a plain
             x=$1 does, rather than splitting into several arguments. */
          let assignment = String{expanded_args.allocator()};
          assignment.append(assignment_token->key().view());
          if (assignment_token->is_append() &&
              (is_local_command || is_declare_command))
          {
            /* local creates a fresh local that shadows an outer name, and
               declare may apply the -i attribute on the same command, so the
               name+=value form passes through literally and the builtin
               computes the append after its own effects exist. */
            assignment += '+';
            assignment += '=';
            assignment.append(
                expand_word_for_assignment(assignment_token->value_word())
                    .view());
          } else {
            assignment += '=';
            /* The append form name+=value concatenates onto the name's current
               value, so the string the builtin stores already carries it, the
               way a plain x+=y assignment prepends the prior value. export and
               readonly do not shadow, so the current value is read here
               correctly. */
            if (assignment_token->is_append())
              assignment.append(get_variable_value(assignment_token->key())
                                    .value_or(String{})
                                    .view());
            let const expanded_value =
                expand_word_for_assignment(assignment_token->value_word());
            /* An integer name adds rather than concatenates, so the join wraps
               the appended expression for the arithmetic in the store. */
            if (assignment_token->is_append() &&
                is_integer_variable(assignment_token->key()))
              append_integer_expression(assignment, expanded_value.view());
            else
              assignment.append(expanded_value.view());
          }
          expanded_args.push(steal(assignment));
          continue;
        }
        /* An assignment that appears as an argument, like echo k=$v, is an
           ordinary word. Rebuild it as the literal key, an equals sign, and the
           value segments, so the value still expands instead of staying
           literal. */
        let key_literal = String{StringView{assignment_token->key()}};
        /* A non-declaration command keeps the literal text, so an append form
           such as echo k+=v stays k+=v rather than losing the plus. */
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
         splitting, or globbing straight to the heap argument vector. The common
         literal argument such as '-lt', '200000', 'echo', or a plain filename
         takes this path and never enters expand_word or expand_path. */
      let const do_expand_one_word = [&](const Word &expandable)
                                         throws -> void {
        let const plain_kind = expandable.plain_literal_kind();
        let did_take_fast_path = false;
        if (plain_kind != Word::PlainLiteral::NotPlain) {
          let literal = String{expanded_args.allocator()};
          for (const WordSegment &segment : expandable.segments)
            literal.append(segment.text.view());

          /* A single unquoted segment still needs the IFS check, since an IFS
             byte in its text would split it into more than one field. With no
             IFS byte it is one field. */
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

        if (!did_take_fast_path) {
          for (glob_field &field : expand_word(expandable)) {
            for (String &g : expand_path(steal(field), location))
              expanded_args.push_managed(StringView{g.c_str(), g.count()});
          }
        }
      };

      /* Brace expansion runs first in every mood but POSIX, turning one word
         into the several the braces spell, each then taking the path above.
         The brace scan is skipped when no { is present, so a brace-free word
         pays nothing beyond the cheap check. */
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

  /* The trace goes to standard error, the way bash does it, so it stays out of
     a command substitution's captured output. The plus is repeated once per
     enclosing subshell, so the top shell shows '+', a substitution '++', and a
     nested one '+++'. */
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

} /* namespace shit */
