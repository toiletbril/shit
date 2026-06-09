#include "Lexer.hpp"

#include "Arena.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <cstdio>

/* TODO: Rewrite the lexer and parser to suit the shell language better. */
/* TODO: Cache the token for repeated peeks. */

namespace shit {

namespace lexer {

static constexpr char CEOF = static_cast<char>(EOF);

hot pure fn is_whitespace(char ch) wontthrow -> bool
{
  switch (ch) {
  case ' ':
  case '\v':
  case '\r':
  case '\t': return true;
  default: return false;
  }
}

hot pure fn is_number(char ch) wontthrow -> bool
{
  return ch >= '0' && ch <= '9';
}

hot pure fn is_expression_sentinel(char ch) wontthrow -> bool
{
  switch (ch) {
  case '\n':
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case ')':
  case '(':
  case ';':
  case '~':
  case '&':
  case '|':
  case '>':
  case '<':
  case '^':
  case '=':
  case '.':
  case '!': return true;
  default: return false;
  };
}

/* TODO: Separate redirections from here. */
hot pure fn is_shell_sentinel(char ch) wontthrow -> bool
{
  /* A brace is not a sentinel. POSIX recognizes '{' and '}' as reserved words
     only when a token is exactly '{' or '}' in command position, so the lexer
     keeps a brace as an ordinary identifier character and the parser checks the
     word text. This way 'a{b}c' lexes as one word. */
  switch (ch) {
  case '\n':
  case '|':
  case '(':
  case ')':
  case '&':
  case ';':
  case '<':
  case '>': return true;
  default: return false;
  };
}

hot pure fn is_part_of_identifier(char ch) wontthrow -> bool
{
  return !is_shell_sentinel(ch) && !is_whitespace(ch) && ch != CEOF;
}

/* A byte that the identifier lexer copies verbatim into the current
   UnquotedText segment while outside any quote and outside an escape. It is an
   ordinary identifier byte that does not open a quote, an expansion, a command
   substitution, or an escape, so a run of these bytes lexes to one literal
   append with no per-byte state change. */
hot pure fn is_plain_unquoted_run_byte(char ch) wontthrow -> bool
{
  return is_part_of_identifier(ch) && ch != '$' && ch != '`' && ch != '\\' &&
         ch != '"' && ch != '\'';
}

hot pure fn is_string_quote(char ch) wontthrow -> bool
{
  /* A backtick is not a string quote. It opens a command substitution, which
     the identifier lexer handles inline. */
  switch (ch) {
  case '"':
  case '\'': return true;
  default: return false;
  }
}

hot pure fn is_ascii_char(char ch) wontthrow -> bool
{
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

hot pure fn is_expandable_char(char ch) wontthrow -> bool
{
  switch (ch) {
  case '[':
  case '?':
  case '*': return true;
  default: return false;
  }
}

hot pure fn is_variable_name_start(char ch) wontthrow -> bool
{
  return is_ascii_char(ch) || ch == '_';
}

hot pure fn is_variable_name(char ch) wontthrow -> bool
{
  return is_variable_name_start(ch) || is_number(ch);
}

} /* namespace lexer */

Lexer::Lexer(String source, BumpArena &arena, bool should_collect_debug_words,
             Maybe<StringView> filename)
    : m_source(steal(source)), m_arena(&arena), m_filename(filename),
      m_should_collect_debug_words(should_collect_debug_words)
{}

Lexer::~Lexer()
{
  for (String *body : m_heredoc_bodies)
    delete body;
}

flatten fn Lexer::peek_expression_token() throws -> Token *
{
  skip_whitespace();
  return lex_expression_token();
}

flatten fn Lexer::peek_shell_token() throws -> Token *
{
  skip_whitespace();
  return lex_shell_token();
}

hot fn Lexer::next_expression_token() throws -> Token *
{
  skip_whitespace();

  const let t = lex_expression_token();
  ASSERT(t != nullptr);

  advance_past_last_peek();

  return t;
}

hot fn Lexer::next_shell_token() throws -> Token *
{
  skip_whitespace();

  const let t = lex_shell_token();
  ASSERT(t != nullptr);

  advance_past_last_peek();

  return t;
}

pure fn Lexer::source() const wontthrow -> StringView
{
  return m_source.view();
}

pure fn Lexer::debug_words() const wontthrow -> const ArrayList<Word> &
{
  return m_debug_words;
}

pure fn Lexer::arena() const wontthrow -> BumpArena & { return *m_arena; }

fn Lexer::set_arena(BumpArena &arena) wontthrow -> void { m_arena = &arena; }

hot fn Lexer::advance_past_last_peek() throws -> usize
{
  ASSERT(m_cursor_position + m_cached_offset <= m_source.length());

  const let r = advance_forward(m_cached_offset);
  m_cached_offset = 0;

  /* Consuming the newline that ends a line with a pending heredoc is where the
     body is collected, since the body sits on the following lines. */
  if (m_last_shell_token_was_newline && !m_pending_heredocs.is_empty()) {
    m_last_shell_token_was_newline = false;
    collect_pending_heredocs();
  }

  return r;
}

cold fn Lexer::register_heredoc(StringView delimiter, bool strip_tabs) throws
    -> const String *
{
  let body = new String{};
  ASSERT(body != nullptr);

  m_heredoc_bodies.push(body);
  m_pending_heredocs.push({String{delimiter}, strip_tabs, body});

  return body;
}

cold fn Lexer::collect_pending_heredocs() throws -> void
{
  for (heredoc_pending &pending : m_pending_heredocs) {
    /* The body is written into the lexer-owned String the parsed redirection
       points at, so it accumulates as one. */
    String collected{};
    for (;;) {
      if (m_cursor_position >= m_source.length()) break;

      const let line_start = m_cursor_position;
      ASSERT(line_start <= m_source.length());

      usize i = line_start;
      while (i < m_source.length() && m_source[i] != '\n')
        i++;
      const let has_newline = (i < m_source.length());
      m_cursor_position = has_newline ? i + 1 : i;

      usize line_offset = line_start;
      usize line_length = i - line_start;
      if (pending.strip_tabs) {
        while (line_length > 0 && m_source[line_offset] == '\t') {
          line_offset++;
          line_length--;
        }
      }

      const let line = m_source.substring_of_length(line_offset, line_length);
      if (pending.delimiter == line) break;
      collected.append(line);
      collected += '\n';
    }
    ASSERT(pending.body != nullptr);
    *pending.body = steal(collected);
  }
  m_pending_heredocs.clear();
}

hot fn Lexer::lex_expression_token() throws -> Token *
{
  if (const let ch = chop_character(); ch != lexer::CEOF) [[likely]] {
    if (lexer::is_number(ch))
      return lex_number();
    else if (lexer::is_expression_sentinel(ch))
      return lex_sentinel();
    else if (lexer::is_part_of_identifier(ch))
      return lex_identifier();
    else
      [[unlikely]] throw ErrorWithLocation{here(m_cursor_position, 1),
                                           "Unexpected character"};
  }

  return m_arena->create<tokens::EndOfFile>(here(m_cursor_position, 1));
}

hot flatten fn Lexer::lex_shell_token() throws -> Token *
{
  Token *t{};
  if (const let ch = chop_character(); ch != lexer::CEOF) [[likely]] {
    if (lexer::is_shell_sentinel(ch))
      t = lex_sentinel();
    else if (lexer::is_part_of_identifier(ch)) [[likely]]
      t = lex_identifier();
    else
      [[unlikely]] throw ErrorWithLocation{here(m_cursor_position, 1),
                                           "Unexpected character"};
  } else {
    t = m_arena->create<tokens::EndOfFile>(here(m_cursor_position, 1));
  }

  ASSERT(t != nullptr);

  m_last_shell_token_was_newline = (t->kind() == Token::Kind::Newline);

  return t;
}

hot flatten forceinline fn Lexer::skip_whitespace() wontthrow -> void
{
  usize i = 0;
  for (;;) {
    while (lexer::is_whitespace(chop_character(i)))
      i++;
    /* A backslash before a newline continues the line and both bytes vanish.
       Stripping it here at the token boundary means a continuation between a
       case-pattern '|' and the next alternative reads as the join it is, the
       same as dash. A backslash before any other byte is left for the
       identifier lexer to treat as an escape. */
    if (chop_character(i) == '\\' && chop_character(i + 1) == '\n') {
      i += 2;
      continue;
    }
    /* A '#' at a token boundary begins a comment that runs to the end of the
       line. The newline is left in place so it still terminates the command,
       and a leading '#!' shebang is just the first such comment. */
    if (chop_character(i) == '#') {
      while (chop_character(i) != '\n' && chop_character(i) != lexer::CEOF)
        i++;
      continue;
    }
    break;
  }
  advance_forward(i);
}

hot forceinline fn Lexer::advance_forward(usize offset) wontthrow -> usize
{
  ASSERT(m_cursor_position + offset <= m_source.length());
  m_cursor_position += offset;
  return offset;
}

hot forceinline fn Lexer::chop_character(usize offset) wontthrow -> char
{
  if (m_cursor_position + offset < m_source.length())
    return m_source[m_cursor_position + offset];

  return lexer::CEOF;
}

hot fn Lexer::lex_number() throws -> Token *
{
  char ch;
  String digits{};
  usize length = 0;

  while (lexer::is_number((ch = chop_character(length)))) {
    digits += ch;
    length++;
  }

  Token *const num =
      m_arena->create<tokens::Number>(here(m_cursor_position, length), digits);
  ASSERT(num != nullptr);

  m_cached_offset = length;

  return num;
}

/* Hottest function in the entire codebase. */
flatten hot fn Lexer::lex_identifier() throws -> Token *
{
  Word word{};

  usize byte_count = 0, relative_last_quote_char_pos = 0;

  bool should_escape = false;

  Maybe<char> quote_char;

  /* Set when the open quote enclosed at least one character. When the matching
     close quote arrives with it still clear, the quote enclosed nothing and an
     empty segment is synthesized so the word keeps one empty field, the way ""
     and '' each expand to one empty argument rather than to none. */
  bool did_quote_enclose_content = false;

  /* Append a character to the open segment, starting a new one when the kind
     changes. A variable reference never merges, since each one carries its own
     name. */
  auto append_char = [&word](WordSegment::Kind kind, char ch) {
    if (!word.segments.is_empty() && word.segments.back().kind == kind &&
        kind != WordSegment::Kind::VariableReference)
    {
      word.segments.back().text += ch;
    } else {
      String single{};
      single.push(ch);
      word.segments.push(WordSegment{kind, steal(single), false});
    }
  };

  /* Append a whole run of plain unquoted bytes to the current UnquotedText
     segment in one String append, the batched form of calling append_char with
     UnquotedText for each byte. A run only ever extends or starts an
     UnquotedText segment, since the bytes carry no quote and no escape. */
  auto append_unquoted_run = [&word](StringView run) {
    if (!word.segments.is_empty() &&
        word.segments.back().kind == WordSegment::Kind::UnquotedText)
    {
      word.segments.back().text.append(run);
    } else {
      String text{};
      text.append(run);
      word.segments.push(
          WordSegment{WordSegment::Kind::UnquotedText, steal(text), false});
    }
  };

  for (;;) {
    const let ch = chop_character(byte_count);

    const let is_inside_quote_or_escape =
        quote_char.has_value() || should_escape;
    if (!lexer::is_part_of_identifier(ch) &&
        !(is_inside_quote_or_escape && ch != lexer::CEOF))
    {
      break;
    }

    /* The common case in a large script is a run of plain identifier bytes
       outside any quote or escape. Scan the whole run and append it in one
       String append, which removes the per-byte lambda call, the per-byte
       segment-kind branch, and the per-byte String growth the slow path below
       would pay. The run ends at the first byte that opens a quote, an
       expansion, a substitution, or an escape, or that ends the word, and that
       byte falls through to the unchanged per-byte handling. */
    if (!is_inside_quote_or_escape && lexer::is_plain_unquoted_run_byte(ch)) {
      const let run_start = byte_count;
      do {
        byte_count++;
      } while (lexer::is_plain_unquoted_run_byte(chop_character(byte_count)));
      append_unquoted_run(m_source.view().substring_of_length(
          m_cursor_position + run_start, byte_count - run_start));
      continue;
    }

    if (should_escape) {
      /* The previous character was a backslash. A backslash before a newline
         continues the line and leaves None behind, so the newline byte is
         consumed without appending anything. */
      should_escape = false;
      if (ch != '\n')
        append_char(WordSegment::Kind::LiteralText, ch);
      byte_count++;
      continue;
    }

    if (quote_char == '\'') {
      /* Single quotes take everything literally up to the closing quote. */
      if (ch == '\'') {
        if (!did_quote_enclose_content)
          word.segments.push(
              WordSegment{WordSegment::Kind::LiteralText, String{}, false});
        quote_char.reset();
      } else {
        append_char(WordSegment::Kind::LiteralText, ch);
        did_quote_enclose_content = true;
      }
      byte_count++;
      continue;
    }

    if (ch == '\\') {
      /* Inside double quotes a backslash only escapes $, `, ", \, and a
         newline. Before any other character it stays a literal backslash, so
         "\n" is a backslash and an n, not an escape. Outside double quotes a
         backslash escapes the next character. */
      if (quote_char == '"') {
        did_quote_enclose_content = true;
        const let escaped_next = chop_character(byte_count + 1);
        if (escaped_next == '$' || escaped_next == '`' || escaped_next == '"' ||
            escaped_next == '\\' || escaped_next == '\n')
        {
          should_escape = true;
        } else {
          append_char(WordSegment::Kind::DoubleQuotedText, '\\');
        }
        byte_count++;
        continue;
      }
      should_escape = true;
      byte_count++;
      continue;
    }

    const let is_in_double_quotes = quote_char == '"';

    if (is_in_double_quotes && ch == '"') {
      if (!did_quote_enclose_content)
        word.segments.push(
            WordSegment{WordSegment::Kind::DoubleQuotedText, String{}, false});
      quote_char.reset();
      byte_count++;
      continue;
    }

    /* Past the close quote, any character reached while still inside the double
       quote is enclosed content, whether it is a literal, an expansion, or a
       substitution. */
    if (is_in_double_quotes) did_quote_enclose_content = true;

    if (!quote_char && lexer::is_string_quote(ch)) {
      relative_last_quote_char_pos = byte_count;
      did_quote_enclose_content = false;
      quote_char = ch;
      byte_count++;
      continue;
    }

    if (ch == '$') {
      byte_count++;
      char next = chop_character(byte_count);

      if (next == '(') {
        /* Command substitution. Scan to the matching close paren, honoring
           quotes and nesting so an inner ) does not end it early. */
        byte_count++;

        /* $(( starts arithmetic expansion. Scan to the matching )), allowing
           grouping parens inside. A subshell substitution is written with a
           space, $( (cmd) ). */
        if (chop_character(byte_count) == '(') {
          byte_count++;
          String arithmetic{};
          usize group_depth = 0;
          for (;;) {
            const let c = chop_character(byte_count);
            if (c == lexer::CEOF) [[unlikely]] {
              throw ErrorWithLocationAndDetails{
                  here(m_cursor_position, byte_count),
                  "Unterminated arithmetic expansion",
                  here(m_cursor_position + byte_count, 1), "Expected )) here"};
            }
            if (c == '(') {
              group_depth++;
              arithmetic += c;
              byte_count++;
            } else if (c == ')' && group_depth > 0) {
              group_depth--;
              arithmetic += c;
              byte_count++;
            } else if (c == ')' && chop_character(byte_count + 1) == ')') {
              byte_count += 2;
              break;
            } else {
              arithmetic += c;
              byte_count++;
            }
          }
          word.segments.push(WordSegment{WordSegment::Kind::ArithmeticExpansion,
                                         steal(arithmetic),
                                         is_in_double_quotes});
          continue;
        }

        String inner{};
        usize depth = 1;
        char quote = 0;
        for (;;) {
          const let c = chop_character(byte_count);
          if (c == lexer::CEOF) [[unlikely]] {
            throw ErrorWithLocationAndDetails{
                here(m_cursor_position, byte_count),
                "Unterminated command substitution",
                here(m_cursor_position + byte_count, 1), "Expected ) here"};
          }
          byte_count++;

          if (quote != 0) {
            if (c == quote) quote = 0;
            inner += c;
            continue;
          }
          if (c == '\\') {
            inner += c;
            const let escaped = chop_character(byte_count);
            if (escaped != lexer::CEOF) {
              byte_count++;
              inner += escaped;
            }
            continue;
          }
          if (c == '\'' || c == '"') {
            quote = c;
            inner += c;
            continue;
          }
          if (c == '(') {
            depth++;
          } else if (c == ')') {
            depth--;
            if (depth == 0) break;
          }
          inner += c;
        }
        word.segments.push(WordSegment{WordSegment::Kind::CommandSubstitution,
                                       steal(inner), is_in_double_quotes});
      } else if (next == '{') {
        byte_count++;
        String name{};
        /* The matching close brace lives at brace depth one, so a nested
           ${...} in a default or alternate word does not end the outer
           expansion early. A bare { does not raise the depth, matching dash,
           which closes ${x:-a{b} at the first brace. A nested $(...),
           $((...)), or backtick is copied as a balanced unit so a } inside it
           is never counted. A single quote run, a double quote run, and a
           backslash escape keep their bytes literal so a } they contain is
           never counted either. */
        usize brace_depth = 1;
        char quote = 0;
        for (;;) {
          const let c = chop_character(byte_count);
          if (c == lexer::CEOF) [[unlikely]] {
            throw ErrorWithLocationAndDetails{
                here(m_cursor_position + byte_count, 1),
                "Unterminated variable expansion",
                here(m_cursor_position + byte_count, 1), "Expected } here"};
          }
          byte_count++;

          if (quote == '\'') {
            if (c == '\'') quote = 0;
            name += c;
            continue;
          }
          if (c == '\\') {
            name += c;
            const let escaped = chop_character(byte_count);
            if (escaped != lexer::CEOF) {
              byte_count++;
              name += escaped;
            }
            continue;
          }
          if (quote == '"') {
            if (c == '"') quote = 0;
            name += c;
            continue;
          }
          if (c == '\'' || c == '"') {
            quote = c;
            name += c;
            continue;
          }
          if (c == '`') {
            name += c;
            for (;;) {
              const let b = chop_character(byte_count);
              if (b == lexer::CEOF) break;
              byte_count++;
              name += b;
              if (b == '\\') {
                const let escaped = chop_character(byte_count);
                if (escaped != lexer::CEOF) {
                  byte_count++;
                  name += escaped;
                }
                continue;
              }
              if (b == '`') break;
            }
            continue;
          }
          if (c == '$' && chop_character(byte_count) == '(') {
            /* Copy a nested $(...) or $((...)) by paren balance. */
            name += c;
            name += chop_character(byte_count);
            byte_count++;
            usize paren_depth = 1;
            for (;;) {
              const let p = chop_character(byte_count);
              if (p == lexer::CEOF) break;
              byte_count++;
              name += p;
              if (p == '(')
                paren_depth++;
              else if (p == ')') {
                paren_depth--;
                if (paren_depth == 0) break;
              }
            }
            continue;
          }
          if (c == '$' && chop_character(byte_count) == '{') {
            brace_depth++;
            name += c;
            name += chop_character(byte_count);
            byte_count++;
            continue;
          }
          if (c == '}') {
            brace_depth--;
            if (brace_depth == 0) break;
            name += c;
            continue;
          }
          name += c;
        }
        word.segments.push(WordSegment{WordSegment::Kind::VariableReference,
                                       steal(name), is_in_double_quotes});
      } else if (lexer::is_variable_name_start(next)) {
        String name{};
        while (lexer::is_variable_name(next = chop_character(byte_count))) {
          name += next;
          byte_count++;
        }
        word.segments.push(WordSegment{WordSegment::Kind::VariableReference,
                                       steal(name), is_in_double_quotes});
      } else if (next == '?' || next == '@' || next == '*' || next == '#' ||
                 next == '$' || next == '!' || next == '-' ||
                 lexer::is_number(next))
      {
        byte_count++;
        String special{};
        special.push(next);
        word.segments.push(WordSegment{WordSegment::Kind::VariableReference,
                                       steal(special), is_in_double_quotes});
      } else {
        /* A dollar sign that names None stays a literal dollar sign. */
        append_char(is_in_double_quotes ? WordSegment::Kind::DoubleQuotedText
                                        : WordSegment::Kind::UnquotedText,
                    '$');
      }
      continue;
    }

    if (ch == '`') {
      /* A backtick opens an old-style command substitution. Single quotes take
         everything literally and are handled above, so reaching here means the
         backtick is unquoted or inside double quotes, where it stays active.
         The region runs to the next unescaped backtick, and the POSIX backquote
         unescaping strips a backslash that precedes a backtick, a dollar sign,
         or another backslash. The unescaped bytes form the inner command source
         and reuse the same CommandSubstitution segment as $(...). */
      const let relative_open_backtick_pos = byte_count;
      byte_count++;
      String inner{};
      for (;;) {
        const let c = chop_character(byte_count);
        if (c == lexer::CEOF) [[unlikely]] {
          throw ErrorWithLocationAndDetails{
              here(m_cursor_position + relative_open_backtick_pos, 1),
              "Unterminated command substitution",
              here(m_cursor_position + byte_count, 1), "Expected ` here"};
        }
        if (c == '`') {
          byte_count++;
          break;
        }
        if (c == '\\') {
          const let escaped = chop_character(byte_count + 1);
          if (escaped == '`' || escaped == '$' || escaped == '\\') {
            inner += escaped;
            byte_count += 2;
            continue;
          }
        }
        inner += c;
        byte_count++;
      }
      word.segments.push(WordSegment{WordSegment::Kind::CommandSubstitution,
                                     steal(inner), is_in_double_quotes});
      continue;
    }

    append_char(is_in_double_quotes ? WordSegment::Kind::DoubleQuotedText
                                    : WordSegment::Kind::UnquotedText,
                ch);
    byte_count++;
  }

  if (quote_char) [[unlikely]] {
    String expected_quote{};
    expected_quote += "Expected ";
    expected_quote += *quote_char;
    expected_quote += " here";
    throw ErrorWithLocationAndDetails{
        here(m_cursor_position + relative_last_quote_char_pos,
             sub_sat(byte_count, relative_last_quote_char_pos)),
        "Unterminated string literal", here(m_cursor_position + byte_count, 1),
        expected_quote};
  }

  if (should_escape) [[unlikely]] {
    throw ErrorWithLocationAndDetails{
        here(m_cursor_position + byte_count - 1, 1), "Nothing to escape",
        here(m_cursor_position + byte_count, 1), "Expected a character here"};
  }

  /* The token spans from the word's first byte over byte_count bytes, the
     continuation backslash and newline included, so the location starts at the
     cursor rather than past the continuations. An earlier form shifted the start
     by the continuation count, which moved a continued word's caret off its
     first byte onto a later line. The keyword token macro reads this name too,
     so it stays a local. */
  const let actual_cursor_position = m_cursor_position;
  ASSERT(actual_cursor_position <= m_source.length());

  if (m_should_collect_debug_words &&
      m_cursor_position != m_last_collected_word_position)
  {
    m_debug_words.push(word);
    m_last_collected_word_position = m_cursor_position;
  }

  Token *t{};

  if (auto assignment_split = word.get_assignment_split();
      assignment_split.has_value())
  {
    t = m_arena->create<tokens::Assignment>(
        here(actual_cursor_position, byte_count), assignment_split->name,
        steal(assignment_split->value), assignment_split->is_append);
  } else if (word.segments.count() == 1 &&
             word.segments[0].kind == WordSegment::Kind::UnquotedText)
  {
    /* A bare word may name a keyword. A quoted or escaped word never does, so
       only a single unquoted segment qualifies. */
    const String &word_text = word.segments[0].text;
    if (const let kw =
            KEYWORDS.find(StringView{word_text.data(), word_text.count()}))
    {
      switch (*kw) {
        KW_SWITCH_CASES();
      default: unreachable("unhandled keyword of type %d", ENUM(*kw));
      }
    }
  }

  if (t == nullptr) {
    t = m_arena->create<tokens::WordToken>(
        here(actual_cursor_position, byte_count), steal(word));
  }

  m_cached_offset = byte_count;

  return t;
}

/* Only single-character operators are defined here. Further parsing is done in
 * related routines. */

/* The token kind a single operator character begins, or None when the
   character is not an operator. The switch keeps this allocation free and the
   compiler lowers it to a jump table. */
hot pure static fn lookup_operator(char ch) wontthrow -> Maybe<Token::Kind>
{
  switch (ch) {
  case ')': return Token::Kind::RightParen;
  case '(': return Token::Kind::LeftParen;
  case ']': return Token::Kind::RightSquareBracket;
  case '[': return Token::Kind::LeftSquareBracket;
  case ';': return Token::Kind::Semicolon;
  case '.': return Token::Kind::Dot;
  case '\n': return Token::Kind::Newline;
  case '+': return Token::Kind::Plus;
  case '-': return Token::Kind::Minus;
  case '*': return Token::Kind::Asterisk;
  case '/': return Token::Kind::Slash;
  case '%': return Token::Kind::Percent;
  case '~': return Token::Kind::Tilde;
  case '^': return Token::Kind::Cap;
  case '!': return Token::Kind::ExclamationMark;
  case '&': return Token::Kind::Ampersand;
  case '>': return Token::Kind::Greater;
  case '<': return Token::Kind::Less;
  case '|': return Token::Kind::Pipe;
  case '=': return Token::Kind::Equals;
  default: return None;
  }
}

hot fn Lexer::lex_sentinel() throws -> Token *
{
  const let ch = chop_character();
  ASSERT(ch != lexer::CEOF);

  usize extra_length = 0;

  Token *tok{};

  /* clang-format off */
#define TOKEN_CASE_ONE(t)                                                      \
  case Token::Kind::t:                                                         \
    tok = m_arena->create<tokens::t>(here(m_cursor_position, 1));              \
    break;

#define TOKEN_CASE_TWO(t, ch, t2)                                              \
  case Token::Kind::t: {                                                       \
    if (chop_character(1) == ch) {                                             \
      tok = m_arena->create<tokens::t2>(here(m_cursor_position, 2));           \
      extra_length++;                                                          \
    } else {                                                                   \
      tok = m_arena->create<tokens::t>(here(m_cursor_position, 1));            \
    }                                                                          \
  } break;

#define TOKEN_CASE_THREE(t, ch2, t2, ch3, t3)                                  \
  case Token::Kind::t: {                                                       \
    if (chop_character(1) == ch2) {                                            \
      tok = m_arena->create<tokens::t2>(here(m_cursor_position, 2));           \
      extra_length++;                                                          \
    } else if (chop_character(1) == ch3) {                                     \
      tok = m_arena->create<tokens::t3>(here(m_cursor_position, 2));           \
      extra_length++;                                                          \
    } else {                                                                   \
      tok = m_arena->create<tokens::t>(here(m_cursor_position, 1));            \
    }                                                                          \
  } break;
  /* clang-format on */

  if (const let op = lookup_operator(ch)) {
    switch (*op) {
      TOKEN_CASE_ONE(RightParen);
      TOKEN_CASE_ONE(LeftParen);
      TOKEN_CASE_TWO(Semicolon, ';', DoubleSemicolon);
      TOKEN_CASE_ONE(Dot);
      TOKEN_CASE_ONE(Newline);
      TOKEN_CASE_ONE(Plus);
      TOKEN_CASE_ONE(Minus);
      TOKEN_CASE_ONE(Asterisk);
      TOKEN_CASE_ONE(Slash);
      TOKEN_CASE_ONE(Percent);
      TOKEN_CASE_ONE(Tilde);
      TOKEN_CASE_ONE(Cap);

      TOKEN_CASE_TWO(RightSquareBracket, ']', DoubleRightSquareBracket);
      TOKEN_CASE_TWO(LeftSquareBracket, '[', DoubleLeftSquareBracket);
      TOKEN_CASE_TWO(ExclamationMark, '=', ExclamationEquals);
      TOKEN_CASE_TWO(Ampersand, '&', DoubleAmpersand);
      TOKEN_CASE_TWO(Pipe, '|', DoublePipe);
      TOKEN_CASE_TWO(Equals, '=', DoubleEquals);

      TOKEN_CASE_THREE(Greater, '>', DoubleGreater, '=', GreaterEquals);
      TOKEN_CASE_THREE(Less, '<', DoubleLess, '=', LessEquals);

    default: unreachable("unhandled operator of type %d", ENUM(*op));
    }
  } else {
    String s{};
    s += "unknown operator '";
    s += ch;
    s += "'";
    throw ErrorWithLocation{here(m_cursor_position, extra_length), s};
  }

  ASSERT(tok != nullptr);

  m_cached_offset = 1 + extra_length;

  return tok;
}

} /* namespace shit */
