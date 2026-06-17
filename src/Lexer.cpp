#include "Lexer.hpp"

#include "Arena.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

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

pure fn is_special_parameter_char(char ch) wontthrow -> bool
{
  return ch == '?' || ch == '!' || ch == '#' || ch == '$' || ch == '*' ||
         ch == '@' || ch == '-';
}

} /* namespace lexer */

Lexer::Lexer(String source, BumpArena &arena, bool should_collect_debug_words,
             Maybe<StringView> filename, mimic_mood mood)
    : m_source(steal(source)), m_arena(&arena), m_filename(filename),
      m_mood(mood), m_should_collect_debug_words(should_collect_debug_words)
{
  LOG(Debug, "starting a lexer over %zu bytes of source", m_source.length());
}

Lexer::~Lexer() = default;

flatten fn Lexer::peek_expression_token() throws -> Token *
{
  skip_whitespace();
  if (m_peek_cache != nullptr && !m_peek_cache_is_shell &&
      m_peek_cache_position == m_cursor_position)
    return m_peek_cache;
  Token *const t = lex_expression_token();
  m_peek_cache = t;
  m_peek_cache_is_shell = false;
  m_peek_cache_position = m_cursor_position;
  return t;
}

flatten fn Lexer::peek_shell_token() throws -> Token *
{
  skip_whitespace();
  if (m_peek_cache != nullptr && m_peek_cache_is_shell &&
      m_peek_cache_position == m_cursor_position)
    return m_peek_cache;
  Token *const t = lex_shell_token();
  m_peek_cache = t;
  m_peek_cache_is_shell = true;
  m_peek_cache_position = m_cursor_position;
  return t;
}

hot fn Lexer::next_expression_token() throws -> Token *
{
  skip_whitespace();

  Token *const t = (m_peek_cache != nullptr && !m_peek_cache_is_shell &&
                    m_peek_cache_position == m_cursor_position)
                       ? m_peek_cache
                       : lex_expression_token();
  ASSERT(t != nullptr);

  advance_past_last_peek();

  return t;
}

hot fn Lexer::next_shell_token() throws -> Token *
{
  skip_whitespace();

  Token *const t = (m_peek_cache != nullptr && m_peek_cache_is_shell &&
                    m_peek_cache_position == m_cursor_position)
                       ? m_peek_cache
                       : lex_shell_token();
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

fn Lexer::set_arena(BumpArena &arena) wontthrow -> void
{
  LOG(Debug, "switching the lexer arena and dropping the cached peek");
  m_arena = &arena;
  /* The cached token lives in the old arena, so it must not survive the swap.
   */
  m_peek_cache = nullptr;
}

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
  /* The body lives in the same arena as the parsed nodes that point at it, so
     its lifetime matches the AST. A command substitution caches its parsed tree
     past the temporary lexer that built it, and a lexer-owned heap body freed
     in ~Lexer would dangle behind the cached Redirection. */
  let body = m_arena->create<String>();
  ASSERT(body != nullptr);

  LOG(Debug, "registering a pending heredoc with delimiter '%.*s'",
      static_cast<int>(delimiter.length), delimiter.data);

  m_pending_heredocs.push({String{delimiter}, strip_tabs, body});

  return body;
}

cold fn Lexer::collect_pending_heredocs() throws -> void
{
  LOG(Debug, "collecting %zu pending heredoc bodies",
      m_pending_heredocs.count());

  for (heredoc_pending &pending : m_pending_heredocs) {
    /* The body is written into the lexer-owned String the parsed redirection
       points at, so it accumulates as one. */
    let collected = String{};
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
    LOG(Debug, "capturing a heredoc body of %zu bytes for delimiter '%s'",
        collected.count(), pending.delimiter.c_str());
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
    else [[unlikely]]
      throw ErrorWithLocation{here(m_cursor_position, 1),
                              "Unexpected character"};
  }

  return m_arena->create<tokens::EndOfFile>(here(m_cursor_position, 1));
}

hot flatten fn Lexer::lex_shell_token() throws -> Token *
{
  Token *t{};
  if (const let ch = chop_character(); ch != lexer::CEOF) [[likely]] {
    /* A <(...) or >(...) process substitution is not valid POSIX, so it is a
       pure addition that is active in the default mode as well as bash mode,
       the way [[ ]] and (( )) are. The < or > opens it only when a ( follows
       with no space. */
    if ((ch == '<' || ch == '>') && chop_character(1) == '(') {
      t = lex_process_substitution(ch);
    } else if (lexer::is_shell_sentinel(ch)) {
      t = lex_sentinel();
    } else if (lexer::is_part_of_identifier(ch)) [[likely]] {
      t = lex_identifier();
    } else [[unlikely]] {
      throw ErrorWithLocation{here(m_cursor_position, 1),
                              "Unexpected character"};
    }
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
  let digits = String{};
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
  let word = Word{};

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
  auto do_append_char = [&word](WordSegment::Kind kind, char ch) {
    if (!word.segments.is_empty() && word.segments.back().kind == kind &&
        kind != WordSegment::Kind::VariableReference)
    {
      word.segments.back().text += ch;
    } else {
      let single = String{};
      single.push(ch);
      word.segments.push(WordSegment{kind, steal(single), false});
    }
  };

  /* Append a whole run of plain unquoted bytes to the current UnquotedText
     segment in one String append, the batched form of calling do_append_char
     with UnquotedText for each byte. A run only ever extends or starts an
     UnquotedText segment, since the bytes carry no quote and no escape. */
  auto do_append_unquoted_run = [&word](StringView run) {
    if (!word.segments.is_empty() &&
        word.segments.back().kind == WordSegment::Kind::UnquotedText)
    {
      word.segments.back().text.append(run);
    } else {
      let text = String{};
      text.append(run);
      word.segments.push(
          WordSegment{WordSegment::Kind::UnquotedText, steal(text), false});
    }
  };

  /* The word so far is a bare array name, the left side of a NAME[subscript]
     reference, when it is one unquoted run that reads as a variable name. */
  auto do_word_is_plain_array_name = [&word]() -> bool {
    if (word.segments.count() != 1) return false;
    const WordSegment &segment = word.segments[0];
    if (segment.kind != WordSegment::Kind::UnquotedText ||
        segment.text.is_empty())
    {
      return false;
    }
    if (!lexer::is_variable_name_start(segment.text.view()[0])) return false;
    for (usize i = 1; i < segment.text.count(); i++)
      if (!lexer::is_variable_name(segment.text.view()[i])) return false;
    return true;
  };

  /* A balanced [ ... ] starting at the offset closes on a ] that is followed by
     = or +=, the shape of an array element assignment. The lookahead consumes
     nothing, it only decides whether the bracket run is a protected subscript.
   */
  auto do_subscript_closes_with_assignment = [this](usize start) -> bool {
    usize offset = start + 1;
    usize depth = 1;
    while (depth > 0) {
      const char c = chop_character(offset);
      if (c == lexer::CEOF) return false;
      offset++;
      if (c == '[')
        depth++;
      else if (c == ']')
        depth--;
    }
    const char after = chop_character(offset);
    return after == '=' || (after == '+' && chop_character(offset + 1) == '=');
  };

  /* Advance offset past a balanced open and close pair, the one nested inside a
     subscript or an extended-glob group, stopping at the matching close or at
     the end of the source. */
  auto do_scan_to_matched_close = [this](usize &offset, char open,
                                         char close) -> void {
    usize depth = 1;
    while (depth > 0) {
      const char c = chop_character(offset);
      if (c == lexer::CEOF) break;
      offset++;
      if (c == open)
        depth++;
      else if (c == close)
        depth--;
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

    /* A NAME[subscript]= assignment protects the subscript's operators so a
       bitmask subscript such as key[a|b]=1 stays in the word. It runs only when
       the word so far is a bare array name and a balanced ] followed by = or +=
       marks an assignment, so a glob like x[1|2] in argument position still
       splits. */
    if (!is_inside_quote_or_escape && ch == '[' &&
        do_word_is_plain_array_name() &&
        do_subscript_closes_with_assignment(byte_count))
    {
      const let subscript_start = byte_count;
      byte_count++; /* the [ */
      do_scan_to_matched_close(byte_count, '[', ']');
      do_append_unquoted_run(m_source.view().substring_of_length(
          m_cursor_position + subscript_start, byte_count - subscript_start));
      continue;
    }

    /* An extended-glob group such as @(a|b) is captured whole so its (, nested
       |, and ) stay in the word for the matcher. The opener is one of ?*+@!
       followed with no space by (. The matcher honors the group only under shopt
       extglob, so the capture is a pure addition. */
    if (!is_inside_quote_or_escape &&
        (ch == '?' || ch == '*' || ch == '+' || ch == '@' || ch == '!') &&
        chop_character(byte_count + 1) == '(')
    {
      const let group_start = byte_count;
      byte_count += 2; /* the opener and the ( */
      do_scan_to_matched_close(byte_count, '(', ')');
      do_append_unquoted_run(m_source.view().substring_of_length(
          m_cursor_position + group_start, byte_count - group_start));
      continue;
    }

    /* A run of plain identifier bytes is the common case in a large script.
       Scanning the whole run and appending it once removes the per-byte lambda
       call and String growth the slow path pays. The run ends at the first byte
       that opens a quote, expansion, substitution, or escape. */
    if (!is_inside_quote_or_escape && lexer::is_plain_unquoted_run_byte(ch)) {
      const let run_start = byte_count;
      /* The run stops before an extended-glob opener such as the ? of ?(, so
         the opener reaches the group capture above on the next turn rather than
         being swallowed as a plain byte. */
      auto do_opens_extglob_at = [this](usize offset) -> bool {
        const char c = chop_character(offset);
        return (c == '?' || c == '*' || c == '+' || c == '@' || c == '!') &&
               chop_character(offset + 1) == '(';
      };
      while (!do_opens_extglob_at(byte_count)) {
        byte_count++;
        const char next = chop_character(byte_count);
        /* The run stops before a '[' so the assignment-subscript capture above
           can decide whether to protect the bracket group, the way it stops
           before an extglob opener. */
        if (next == '[' || !lexer::is_plain_unquoted_run_byte(next)) {
          break;
        }
      }
      do_append_unquoted_run(m_source.view().substring_of_length(
          m_cursor_position + run_start, byte_count - run_start));
      continue;
    }

    if (should_escape) {
      /* The previous character was a backslash. A backslash before a newline
         continues the line and leaves None behind, so the newline byte is
         consumed without appending anything. */
      should_escape = false;
      if (ch != '\n') do_append_char(WordSegment::Kind::LiteralText, ch);
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
        do_append_char(WordSegment::Kind::LiteralText, ch);
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
          do_append_char(WordSegment::Kind::DoubleQuotedText, '\\');
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

      /* $'...' is bash ANSI-C quoting. The backslash escapes are decoded here
         and the result is a final literal segment that neither expands nor
         globs, the way single quotes produce a literal but with escapes. A
         pure addition, so it rides every mood but POSIX. */
      if (next == '\'' && bash_additions_enabled()) {
        byte_count++;
        bool did_emit_any = false;
        auto do_emit_literal = [&](char byte) {
          do_append_char(WordSegment::Kind::LiteralText, byte);
          did_emit_any = true;
        };
        auto do_hex_value = [](char h) -> i32 {
          if (h >= '0' && h <= '9') return h - '0';
          if (h >= 'a' && h <= 'f') return h - 'a' + 10;
          if (h >= 'A' && h <= 'F') return h - 'A' + 10;
          return -1;
        };
        auto do_emit_codepoint = [&](u32 cp) {
          if (cp < 0x80) {
            do_emit_literal(static_cast<char>(cp));
          } else if (cp < 0x800) {
            do_emit_literal(static_cast<char>(0xC0 | (cp >> 6)));
            do_emit_literal(static_cast<char>(0x80 | (cp & 0x3F)));
          } else if (cp < 0x10000) {
            do_emit_literal(static_cast<char>(0xE0 | (cp >> 12)));
            do_emit_literal(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            do_emit_literal(static_cast<char>(0x80 | (cp & 0x3F)));
          } else {
            do_emit_literal(static_cast<char>(0xF0 | (cp >> 18)));
            do_emit_literal(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            do_emit_literal(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            do_emit_literal(static_cast<char>(0x80 | (cp & 0x3F)));
          }
        };

        for (;;) {
          const char c = chop_character(byte_count);
          if (c == lexer::CEOF) {
            throw ErrorWithLocationAndDetails{
                here(m_cursor_position, byte_count),
                "Unterminated $'...' string",
                here(m_cursor_position + byte_count, 1), "Expected ' here"};
          }
          byte_count++;
          if (c == '\'') break;
          if (c != '\\') {
            do_emit_literal(c);
            continue;
          }
          const char e = chop_character(byte_count);
          if (e == lexer::CEOF) {
            do_emit_literal('\\');
            break;
          }
          byte_count++;
          switch (e) {
          case 'n': do_emit_literal('\n'); break;
          case 't': do_emit_literal('\t'); break;
          case 'r': do_emit_literal('\r'); break;
          case 'a': do_emit_literal('\a'); break;
          case 'b': do_emit_literal('\b'); break;
          case 'f': do_emit_literal('\f'); break;
          case 'v': do_emit_literal('\v'); break;
          case 'e':
          case 'E': do_emit_literal('\x1b'); break;
          case '\\': do_emit_literal('\\'); break;
          case '\'': do_emit_literal('\''); break;
          case '"': do_emit_literal('"'); break;
          case '?': do_emit_literal('?'); break;
          case 'x': {
            i32 value = 0;
            i32 digit_count = 0;
            while (digit_count < 2 &&
                   do_hex_value(chop_character(byte_count)) >= 0)
            {
              value = value * 16 + do_hex_value(chop_character(byte_count));
              byte_count++;
              digit_count++;
            }
            if (digit_count == 0) {
              do_emit_literal('\\');
              do_emit_literal('x');
            } else {
              do_emit_literal(static_cast<char>(value));
            }
          } break;
          case 'c': {
            /* Control modifier. bash uppercases the letter then takes the low
               five bits, so \cA is 0x01 and \c[ is the escape 0x1b, while \c?
               is the delete 0x7f. The earlier xor with 0x40 only matched this
               for the bytes 0x40 to 0x5f and was wrong for a digit or
               punctuation. */
            const char k = chop_character(byte_count);
            if (k == lexer::CEOF) {
              do_emit_literal('\\');
              do_emit_literal('c');
              break;
            }
            byte_count++;
            /* A control target written as an escaped backslash, \c\\, is
               ctrl-backslash, so the second backslash is consumed here. */
            const char target = k;
            if (k == '\\' && chop_character(byte_count) == '\\') {
              byte_count++;
            }
            const char upper = (target >= 'a' && target <= 'z')
                                   ? static_cast<char>(target - 'a' + 'A')
                                   : target;
            const u8 control =
                upper == '?' ? static_cast<u8>(0x7fu)
                             : static_cast<u8>(static_cast<u8>(upper) & 0x1fu);
            do_emit_literal(static_cast<char>(control));
          } break;
          case 'u':
          case 'U': {
            const i32 max_digit_count = e == 'u' ? 4 : 8;
            u32 codepoint = 0;
            i32 digit_count = 0;
            while (digit_count < max_digit_count &&
                   do_hex_value(chop_character(byte_count)) >= 0)
            {
              codepoint =
                  codepoint * 16 +
                  static_cast<u32>(do_hex_value(chop_character(byte_count)));
              byte_count++;
              digit_count++;
            }
            if (digit_count == 0) {
              do_emit_literal('\\');
              do_emit_literal(e);
            } else {
              do_emit_codepoint(codepoint);
            }
          } break;
          default:
            if (e >= '0' && e <= '7') {
              i32 value = e - '0';
              i32 digit_count = 1;
              while (digit_count < 3) {
                const char o = chop_character(byte_count);
                if (o < '0' || o > '7') {
                  break;
                }
                value = value * 8 + (o - '0');
                byte_count++;
                digit_count++;
              }
              do_emit_literal(static_cast<char>(value));
            } else {
              do_emit_literal('\\');
              do_emit_literal(e);
            }
            break;
          }
        }
        /* An empty $'' still produces one empty field, the way '' and "" each
           keep an empty argument rather than dropping it. */
        if (!did_emit_any)
          word.segments.push(
              WordSegment{WordSegment::Kind::LiteralText, String{}, false});
        continue;
      }

      /* $"..." is bash locale translation. With no message catalog it is the
         plain double-quoted string, so the dollar is dropped and the following
         quote lexes as an ordinary double quote, with expansions inside still
         active. Inside an existing double quote a $" is a dollar then the close
         quote, so this only fires at the top level. */
      if (next == '"' && is_bash_compatible() && !is_in_double_quotes) {
        continue;
      }

      if (next == '(') {
        /* Command substitution. Scan to the matching close paren, honoring
           quotes and nesting so an inner ) does not end it early. */
        byte_count++;

        /* $(( starts arithmetic expansion. Scan to the matching )), allowing
           grouping parens inside. A subshell substitution is written with a
           space, $( (cmd) ). */
        if (chop_character(byte_count) == '(') {
          byte_count++;
          let arithmetic = String{};
          usize group_depth = 0;
          for (;;) {
            const let c = chop_character(byte_count);
            if (c == lexer::CEOF) [[unlikely]] {
              throw ErrorWithLocationAndDetails{
                  here(m_cursor_position, byte_count),
                  "Unterminated arithmetic expansion",
                  here(m_cursor_position + byte_count, 1), "Expected )) here"};
            }
            /* A backslash escape, a quoted span, a backtick run, and a nested
               $(...) are copied as balanced units so a ) they contain is text
               and does not count toward the grouping depth or close the
               expansion early. */
            if (c == '\\') {
              arithmetic += c;
              byte_count++;
              const let escaped = chop_character(byte_count);
              if (escaped != lexer::CEOF) {
                arithmetic += escaped;
                byte_count++;
              }
            } else if (c == '\'' || c == '"') {
              const let quote = c;
              arithmetic += c;
              byte_count++;
              for (;;) {
                const let q = chop_character(byte_count);
                if (q == lexer::CEOF) break;
                arithmetic += q;
                byte_count++;
                if (quote == '"' && q == '\\') {
                  const let escaped = chop_character(byte_count);
                  if (escaped != lexer::CEOF) {
                    arithmetic += escaped;
                    byte_count++;
                  }
                  continue;
                }
                if (q == quote) break;
              }
            } else if (c == '`') {
              arithmetic += c;
              byte_count++;
              for (;;) {
                const let b = chop_character(byte_count);
                if (b == lexer::CEOF) break;
                arithmetic += b;
                byte_count++;
                if (b == '\\') {
                  const let escaped = chop_character(byte_count);
                  if (escaped != lexer::CEOF) {
                    arithmetic += escaped;
                    byte_count++;
                  }
                  continue;
                }
                if (b == '`') break;
              }
            } else if (c == '$' && chop_character(byte_count + 1) == '(') {
              /* Copy a nested $(...) or $((...)) by paren balance, honoring
                 quotes inside it so an inner ) within a string does not
                 unbalance the count. */
              arithmetic += c;
              byte_count++;
              arithmetic += chop_character(byte_count);
              byte_count++;
              usize paren_depth = 1;
              char nested_quote = 0;
              for (;;) {
                const let p = chop_character(byte_count);
                if (p == lexer::CEOF) break;
                arithmetic += p;
                byte_count++;
                if (nested_quote != 0) {
                  if (nested_quote == '"' && p == '\\') {
                    const let escaped = chop_character(byte_count);
                    if (escaped != lexer::CEOF) {
                      arithmetic += escaped;
                      byte_count++;
                    }
                    continue;
                  }
                  if (p == nested_quote) nested_quote = 0;
                  continue;
                }
                if (p == '\\') {
                  const let escaped = chop_character(byte_count);
                  if (escaped != lexer::CEOF) {
                    arithmetic += escaped;
                    byte_count++;
                  }
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
            } else if (c == '(') {
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

        let inner = String{};
        usize depth = 1;
        char quote = 0;
        /* Tracks the byte before the current one so an unquoted '#' that starts
           a word can be told from one inside a word. A zero sentinel marks the
           start of the substitution, where a '#' also starts a word. */
        char previous_char = 0;
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
            previous_char = c;
            continue;
          }
          if (c == '\\') {
            inner += c;
            const let escaped = chop_character(byte_count);
            if (escaped != lexer::CEOF) {
              byte_count++;
              inner += escaped;
            }
            previous_char = c;
            continue;
          }
          if (c == '\'' || c == '"') {
            quote = c;
            inner += c;
            previous_char = c;
            continue;
          }
          /* An unquoted '#' at a word boundary begins a comment that runs to
             the next newline, the same as the main lexer's skip_whitespace. A
             ')' inside the comment is text, so it must not close the
             substitution. The comment bytes are kept in inner since the inner
             lexer skips them again when it re-lexes the captured source. */
          if (c == '#' &&
              (previous_char == 0 || lexer::is_whitespace(previous_char) ||
               previous_char == '\n'))
          {
            inner += c;
            for (;;) {
              const let comment_char = chop_character(byte_count);
              if (comment_char == lexer::CEOF || comment_char == '\n') {
                break;
              }
              byte_count++;
              inner += comment_char;
            }
            previous_char = '#';
            continue;
          }
          if (c == '(') {
            depth++;
          } else if (c == ')') {
            depth--;
            if (depth == 0) break;
          }
          inner += c;
          previous_char = c;
        }
        word.segments.push(WordSegment{WordSegment::Kind::CommandSubstitution,
                                       steal(inner), is_in_double_quotes});
      } else if (next == '{') {
        byte_count++;
        let name = String{};
        /* A ${ followed by whitespace is the bash 5.3 funsub, a command body
           that runs in the current shell, riding every mood but POSIX like
           the other pure additions. The leading whitespace drops and the
           same balanced walk below finds the close brace. */
        bool is_function_substitution = false;
        if (bash_additions_enabled()) {
          let probe = chop_character(byte_count);
          while (probe == ' ' || probe == '\t' || probe == '\n') {
            is_function_substitution = true;
            byte_count++;
            probe = chop_character(byte_count);
          }
        }
        /* The matching close brace lives at brace depth one, so a nested ${...}
           does not end the outer expansion early. A bare { does not raise the
           depth, matching dash. A nested $(...), $((...)), or backtick is copied
           as a balanced unit, and a quote run or backslash escape keeps its
           bytes literal, so a } inside any of them is never counted. */
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
            /* Copy a nested $(...) or $((...)) by paren balance, honoring
               quotes inside it so an inner ) within a string does not unbalance
               the count. */
            name += c;
            name += chop_character(byte_count);
            byte_count++;
            usize paren_depth = 1;
            char nested_quote = 0;
            for (;;) {
              const let p = chop_character(byte_count);
              if (p == lexer::CEOF) break;
              byte_count++;
              name += p;
              if (nested_quote != 0) {
                if (nested_quote == '"' && p == '\\') {
                  const let escaped = chop_character(byte_count);
                  if (escaped != lexer::CEOF) {
                    byte_count++;
                    name += escaped;
                  }
                  continue;
                }
                if (p == nested_quote) nested_quote = 0;
                continue;
              }
              if (p == '\\') {
                const let escaped = chop_character(byte_count);
                if (escaped != lexer::CEOF) {
                  byte_count++;
                  name += escaped;
                }
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
          if (c == '$' && chop_character(byte_count) == '{') {
            brace_depth++;
            name += c;
            name += chop_character(byte_count);
            byte_count++;
            continue;
          }
          /* The funsub body is command text, so a bare { opens a function
             body or a brace group whose } must not close the substitution,
             while a variable reference keeps the dash reading where only a
             nested ${ raises the depth. */
          if (c == '{' && is_function_substitution) {
            brace_depth++;
            name += c;
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
        word.segments.push(WordSegment{
            is_function_substitution ? WordSegment::Kind::FunctionSubstitution
                                     : WordSegment::Kind::VariableReference,
            steal(name), is_in_double_quotes});
      } else if (lexer::is_variable_name_start(next)) {
        let name = String{};
        while (lexer::is_variable_name(next = chop_character(byte_count))) {
          name += next;
          byte_count++;
        }
        word.segments.push(WordSegment{WordSegment::Kind::VariableReference,
                                       steal(name), is_in_double_quotes});
      } else if (lexer::is_special_parameter_char(next) ||
                 lexer::is_number(next))
      {
        byte_count++;
        let special = String{};
        special.push(next);
        word.segments.push(WordSegment{WordSegment::Kind::VariableReference,
                                       steal(special), is_in_double_quotes});
      } else {
        /* A dollar sign that names None stays a literal dollar sign. */
        do_append_char(is_in_double_quotes ? WordSegment::Kind::DoubleQuotedText
                                           : WordSegment::Kind::UnquotedText,
                       '$');
      }
      continue;
    }

    if (ch == '`') {
      /* A backtick here opens an old-style command substitution, unquoted or
         inside double quotes where it stays active. The region runs to the next
         unescaped backtick, and the POSIX backquote unescaping strips a backslash
         before a backtick, a dollar sign, or another backslash. The unescaped
         bytes reuse the same CommandSubstitution segment as $(...). */
      const let relative_open_backtick_pos = byte_count;
      byte_count++;
      let inner = String{};
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

    do_append_char(is_in_double_quotes ? WordSegment::Kind::DoubleQuotedText
                                       : WordSegment::Kind::UnquotedText,
                   ch);
    byte_count++;
  }

  if (quote_char) [[unlikely]] {
    let expected_quote = String{};
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
     cursor rather than past the continuations. An earlier form shifted the
     start by the continuation count, which moved a continued word's caret off
     its first byte onto a later line. The keyword token macro reads this name
     too, so it stays a local. */
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
    /* ; is a separator, ;; ends a case arm, and the bash fall-through forms ;&
       and ;;& continue into the next arm or keep matching. They are a pure
       addition since dash rejects an & after a semicolon, so they stay on in
       every mode the way [[ ]] does. */
    case Token::Kind::Semicolon: {
      if (chop_character(1) == ';') {
        if (chop_character(2) == '&') {
          tok = m_arena->create<tokens::DoubleSemicolonAmpersand>(
              here(m_cursor_position, 3));
          extra_length += 2;
        } else {
          tok = m_arena->create<tokens::DoubleSemicolon>(
              here(m_cursor_position, 2));
          extra_length++;
        }
      } else if (chop_character(1) == '&') {
        tok = m_arena->create<tokens::SemicolonAmpersand>(
            here(m_cursor_position, 2));
        extra_length++;
      } else {
        tok = m_arena->create<tokens::Semicolon>(here(m_cursor_position, 1));
      }
    } break;
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
    /* & is the background operator, && the logical and. &> and &>> redirect
       both standard streams to a file, recognized before the plain & since
       they change the POSIX meaning, so they ride every mood but POSIX the
       way the other pure bash additions do. */
    case Token::Kind::Ampersand: {
      if (bash_additions_enabled() && chop_character(1) == '>') {
        if (chop_character(2) == '>') {
          tok = m_arena->create<tokens::AmpersandDoubleGreater>(
              here(m_cursor_position, 3));
          extra_length += 2;
        } else {
          tok = m_arena->create<tokens::AmpersandGreater>(
              here(m_cursor_position, 2));
          extra_length++;
        }
      } else if (chop_character(1) == '&') {
        tok = m_arena->create<tokens::DoubleAmpersand>(
            here(m_cursor_position, 2));
        extra_length++;
      } else {
        tok = m_arena->create<tokens::Ampersand>(here(m_cursor_position, 1));
      }
    } break;

    /* | is a pipe, || the logical or. |& pipes both standard output and
       standard error, the shorthand for 2>&1 |, riding every mood but POSIX
       the way the other pure bash additions do. */
    case Token::Kind::Pipe: {
      if (chop_character(1) == '|') {
        tok = m_arena->create<tokens::DoublePipe>(here(m_cursor_position, 2));
        extra_length++;
      } else if (bash_additions_enabled() && chop_character(1) == '&') {
        tok =
            m_arena->create<tokens::PipeAmpersand>(here(m_cursor_position, 2));
        extra_length++;
      } else {
        tok = m_arena->create<tokens::Pipe>(here(m_cursor_position, 1));
      }
    } break;
      TOKEN_CASE_TWO(Equals, '=', DoubleEquals);

      TOKEN_CASE_THREE(Greater, '>', DoubleGreater, '=', GreaterEquals);

    /* < is input redirect, << a heredoc, <= a comparison. <<< is the bash
       here-string, which feeds a single expanded word as standard input. It
       changes the POSIX reading of << with a <word delimiter, so POSIX mode
       keeps tokenizing << then <, while the default and bash moods read the
       here-string the way the other pure bash additions ride the default
       mood. */
    case Token::Kind::Less: {
      if (chop_character(1) == '<') {
        if (chop_character(2) == '<' && bash_additions_enabled()) {
          tok = m_arena->create<tokens::TripleLess>(here(m_cursor_position, 3));
          extra_length += 2;
        } else {
          tok = m_arena->create<tokens::DoubleLess>(here(m_cursor_position, 2));
          extra_length++;
        }
      } else if (chop_character(1) == '=') {
        tok = m_arena->create<tokens::LessEquals>(here(m_cursor_position, 2));
        extra_length++;
      } else {
        tok = m_arena->create<tokens::Less>(here(m_cursor_position, 1));
      }
    } break;

    default: unreachable("unhandled operator of type %d", ENUM(*op));
    }
  } else {
    let s = String{};
    s += "unknown operator '";
    s += ch;
    s += "'";
    throw ErrorWithLocation{here(m_cursor_position, extra_length), s};
  }

  ASSERT(tok != nullptr);

  m_cached_offset = 1 + extra_length;

  return tok;
}

hot fn Lexer::lex_process_substitution(char direction) throws -> Token *
{
  let const open_position = m_cursor_position;
  usize byte_count = 0;
  byte_count++; /* the < or > */
  byte_count++; /* the ( */

  /* The direction byte leads the segment text so the evaluator knows which way
     the pipe runs without a second field. */
  let inner = String{};
  inner += direction;

  usize depth = 1;
  char quote = 0;
  for (;;) {
    const char c = chop_character(byte_count);
    if (c == lexer::CEOF) [[unlikely]] {
      throw ErrorWithLocationAndDetails{
          here(open_position, byte_count), "Unterminated process substitution",
          here(open_position + byte_count, 1), "Expected ) here"};
    }
    byte_count++;

    if (quote != 0) {
      if (c == quote) quote = 0;
      inner += c;
      continue;
    }
    if (c == '\\') {
      inner += c;
      const char escaped = chop_character(byte_count);
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
      inner += c;
      continue;
    }
    if (c == ')') {
      depth--;
      if (depth == 0) break;
      inner += c;
      continue;
    }
    inner += c;
  }

  LOG(Debug, "capturing a process substitution of %zu bytes", byte_count);

  let word = Word{};
  word.segments.push(
      WordSegment{WordSegment::Kind::ProcessSubstitution, steal(inner), false});
  let t = m_arena->create<tokens::WordToken>(here(open_position, byte_count),
                                             steal(word));
  m_cached_offset = byte_count;
  return t;
}

} /* namespace shit */
