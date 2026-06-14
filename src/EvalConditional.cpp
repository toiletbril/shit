#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* POSIX regcomp and regexec back the [[ =~ operator. The release build drops
   libstdc++, so std::regex is unavailable and the libc regex is used instead.
 */
#if SHIT_PLATFORM_IS POSIX
#include <regex.h>
#endif

/* _get_osfhandle maps a shell fd number to its Windows handle for the -t test.
 */
#if SHIT_PLATFORM_IS WIN32
#include <io.h>
#endif

namespace shit {

namespace {

/* A recursive-descent evaluator over the [[ ]] element list. The grammar joins
   primaries with && and ||, allows ! and parentheses, and reads unary and
   binary primaries the way the double-bracket conditional does, with no field
   splitting on the operands. */
struct ConditionalEvaluator
{
  EvalContext &cxt;
  const ArrayList<conditional_element> &elements;
  usize pos = 0;
  /* When a && or || branch is already decided, the other side is parsed to
     advance past its tokens but not evaluated, so a glob, a regex, a bad
     integer, or a command substitution on the dead branch runs no side effect
     and raises no evaluation error, the way bash short-circuits [[ ]]. */
  bool is_skipping = false;

  using Kind = conditional_element::Kind;

  pure bool at_end() const wontthrow { return pos >= elements.count(); }
  pure Kind kind_at(usize i) const wontthrow { return elements[i].kind; }

  /* The literal text of the token the evaluator stopped before, so an error
     names what was unexpected rather than leaving the reader to guess. */
  String unexpected_token() throws
  {
    return at_end() ? String{} : operand_literal(elements[pos]);
  }

  /* The literal source text of an operand, used to recognize a word operator
     such as == or -f without expanding it. */
  String operand_literal(const conditional_element &e) throws
  {
    if (e.word != nullptr && e.word->kind() == Token::Kind::Word)
      return static_cast<const tokens::WordToken *>(e.word)
          ->word()
          .to_literal_string();
    if (e.word != nullptr) return e.word->raw_string();
    return String{heap_allocator()};
  }

  /* The expanded value of an operand, with no field splitting. The expansion
     throws a plain Error, an unset variable under set -u, so it is relocated to
     a caret at the operand the way process_args locates a command argument. */
  String operand_value(const conditional_element &e) throws
  {
    if (e.word != nullptr && e.word->kind() == Token::Kind::Word) {
      try {
        return cxt.expand_word_for_assignment(
            static_cast<const tokens::WordToken *>(e.word)->word());
      } catch (const Error &err) {
        throw relocate_error(err, e.word->source_location());
      }
    }
    if (e.word != nullptr) return e.word->raw_string();
    return String{heap_allocator()};
  }

  /* The right side of == or != is a pattern, so it expands with a parallel mask
     that marks which *, ?, and [ stay active. A quoted or escaped metacharacter
     is masked off and matches literally, the way bash treats a quoted RHS. */
  String operand_pattern_masked(const conditional_element &e,
                                ArrayList<bool> &active) throws
  {
    if (e.word != nullptr && e.word->kind() == Token::Kind::Word) {
      try {
        return cxt.expand_case_pattern_masked(
            static_cast<const tokens::WordToken *>(e.word)->word(), active);
      } catch (const Error &err) {
        throw relocate_error(err, e.word->source_location());
      }
    }
    String raw =
        e.word != nullptr ? e.word->raw_string() : String{heap_allocator()};
    for (usize i = 0; i < raw.count(); i++)
      active.push(true);
    return raw;
  }

  static pure bool is_unary_op(StringView s) wontthrow
  {
    return s == "-z" || s == "-n" || s == "-e" || s == "-f" || s == "-d" ||
           s == "-r" || s == "-w" || s == "-x" || s == "-s" || s == "-h" ||
           s == "-L" || s == "-b" || s == "-c" || s == "-p" || s == "-S" ||
           s == "-g" || s == "-u" || s == "-k" || s == "-O" || s == "-G" ||
           s == "-v" || s == "-t" || s == "-o";
  }

  static pure bool is_binary_word_op(StringView s) wontthrow
  {
    return s == "=" || s == "==" || s == "!=" || s == "=~" || s == "-eq" ||
           s == "-ne" || s == "-lt" || s == "-le" || s == "-gt" || s == "-ge" ||
           s == "-ef" || s == "-nt" || s == "-ot";
  }

  /* The extended-regex metacharacters, escaped to a literal when a quoted byte
     of the pattern must match itself. */
  static pure bool is_regex_metacharacter(char c) wontthrow
  {
    return c == '.' || c == '^' || c == '$' || c == '*' || c == '+' ||
           c == '?' || c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '|' || c == '\\';
  }

  /* The =~ operator matches the value against an extended regular expression. A
     POSIX regcomp with the extended grammar mirrors the ERE bash uses, and a
     search rather than a full match finds the pattern anywhere in the value,
     the way [[ =~ does. On a match BASH_REMATCH is filled with the whole match
     at index 0 and each capture group after it, an unmatched group reading as
     an empty string the way bash leaves it. */
  bool regex_match(StringView value, StringView pattern,
                   const ArrayList<bool> &active) throws
  {
#if SHIT_PLATFORM_IS POSIX
    /* A byte the mask marks inactive came from a quoted or escaped part of the
       right operand, so a regex metacharacter there is backslash-escaped to
       match itself, the way bash matches a quoted portion of the operand
       literally. An active byte stays live regex. The escaped pattern is the
       cache key, built null-terminated for the regcomp a miss runs. */
    let escaped_pattern = String{cxt.scratch_allocator()};
    for (usize i = 0; i < pattern.length; i++) {
      const bool is_literal = i < active.count() && !active[i];
      if (is_literal && is_regex_metacharacter(pattern[i]))
        escaped_pattern += '\\';
      escaped_pattern += pattern[i];
    }
    /* The pattern compiles once and is reused on later matches through the
       context cache, so a hot =~ loop pays regcomp only the first time. regexec
       reads a C string, so the value is copied into a null-terminated buffer.
     */
    regex_t *compiled = cxt.cached_compiled_regex(escaped_pattern.view());
    let const value_text = String{cxt.scratch_allocator(), value};
    let const group_count = compiled->re_nsub + 1;
    let matches = ArrayList<regmatch_t>{cxt.scratch_allocator()};
    for (usize i = 0; i < group_count; i++)
      matches.push(regmatch_t{});
    const int match_result =
        regexec(compiled, value_text.c_str(), group_count, matches.begin(), 0);
    LOG(All, "the =~ regex %s the value",
        match_result == 0 ? "matched" : "did not match");
    if (match_result != 0) {
      /* bash clears BASH_REMATCH on a non-match, so a later read does not see a
         prior match's captures. */
      cxt.set_indexed_array("BASH_REMATCH",
                            ArrayList<String>{heap_allocator()});
      return false;
    }

    let rematch = ArrayList<String>{heap_allocator()};
    for (usize i = 0; i < group_count; i++) {
      if (matches[i].rm_so < 0) {
        rematch.push(String{heap_allocator()});
        continue;
      }
      let const start = static_cast<usize>(matches[i].rm_so);
      let const end = static_cast<usize>(matches[i].rm_eo);
      rematch.push(String{heap_allocator(),
                          value.substring_of_length(start, end - start)});
    }
    cxt.set_indexed_array("BASH_REMATCH", steal(rematch));
    return true;
#else
    unused(value);
    unused(pattern);
    unused(active);
    throw Error{"Unable to use =~ in the [[ ]] because it is not supported on "
                "this platform"};
#endif
  }

  bool eval_unary(StringView op, StringView operand) throws
  {
    if (op == "-z") return operand.is_empty();
    if (op == "-n") return !operand.is_empty();
    if (op == "-v") {
      /* -v name[subscript] tests an array element or key, every other -v form
         tests a plain variable. */
      if (let const bracket = operand.find_character('[');
          bracket.has_value() && operand.length > 0 &&
          operand[operand.length - 1] == ']')
      {
        const StringView name = operand.substring_of_length(0, *bracket);
        const StringView subscript = operand.substring_of_length(
            *bracket + 1, operand.length - *bracket - 2);
        return cxt.array_element_is_set(name, subscript);
      }
      return cxt.get_variable_value(operand).has_value();
    }
    let const path = Path{operand};
    if (op == "-e") return path.exists();
    if (op == "-f") return path.is_regular_file();
    if (op == "-d") return path.is_directory();
    if (op == "-r") return path.is_readable();
    if (op == "-w") return path.is_writable();
    if (op == "-x") return path.is_executable();
    if (op == "-s") {
      let const size = path.file_size();
      return size.has_value() && size.value() > 0;
    }
    /* -t tests whether a file descriptor is an open terminal, the way a script
       gates an interactive feature on a real tty. Any descriptor is checked,
       not only the standard three, since a config such as ble.sh dups the
       controlling terminal onto a higher descriptor and tests that. */
    if (op == "-t") {
      if (ErrorOr<i64> descriptor = utils::parse_decimal_integer(operand);
          !descriptor.is_error())
#if SHIT_PLATFORM_IS WIN32
        /* A Windows descriptor is a HANDLE, so the shell fd number is mapped to
           its C runtime handle before the tty check. */
        return os::is_fd_a_tty(reinterpret_cast<os::descriptor>(
            _get_osfhandle(static_cast<int>(descriptor.value()))));
#else
        return os::is_fd_a_tty(static_cast<os::descriptor>(descriptor.value()));
#endif
      /* bash reports a non-integer -t operand as an error with status 2 and
         goes on, so the throw carries that status for the command-level
         catch. */
      let error = Error{"Unable to test '-t " + operand +
                        "' because the operand is not an integer"};
      error.set_command_status(2);
      throw error;
    }
    /* -o tests a shell option by name. Only the emacs line-editing option is
       reported, as on, since shit's interactive editing is emacs style. Every
       other option name reads as off for now, so a config that gates on the
       editing mode such as ble.sh sees emacs and proceeds. */
    if (op == "-o") return operand == "emacs";
    /* The remaining file-type tests fall back to existence, which covers the
       common scripts without a full stat-mode surface. */
    return path.exists();
  }

  bool eval_binary(StringView left, StringView op, StringView right) throws
  {
    if (op == "-ef") return Path{left}.is_same_file_as(Path{right});
    if (op == "-nt") return Path{left}.is_newer_than(Path{right});
    if (op == "-ot") return Path{left}.is_older_than(Path{right});

    /* The arithmetic comparison operands are full arithmetic expressions in
       bash, so 1+1 and a bare variable name evaluate rather than only a literal
       integer. An empty operand reads as zero the way an arithmetic context
       treats an unset value. */
    auto to_number = [&](StringView operand) throws -> i64 {
      for (usize i = 0; i < operand.length; i++)
        if (operand[i] != ' ' && operand[i] != '\t')
          return cxt.evaluate_arithmetic(operand);
      return 0;
    };
    const i64 a = to_number(left);
    const i64 b = to_number(right);
    if (op == "-eq") return a == b;
    if (op == "-ne") return a != b;
    if (op == "-lt") return a < b;
    if (op == "-le") return a <= b;
    if (op == "-gt") return a > b;
    return a >= b;
  }

  bool eval_primary() throws
  {
    if (at_end())
      throw Error{"Unable to evaluate the [[ ]] because the expression ends "
                  "unexpectedly"};
    const conditional_element &first = elements[pos];
    if (first.kind != Kind::Operand)
      throw Error{"Unable to evaluate the [[ ]] because an operator appears "
                  "where an operand is expected"};

    const String first_literal = operand_literal(first);

    /* A unary operator followed by an operand. */
    if (is_unary_op(first_literal.view()) && pos + 1 < elements.count() &&
        kind_at(pos + 1) == Kind::Operand)
    {
      pos += 2;
      if (is_skipping) return false;
      const String operand = operand_value(elements[pos - 1]);
      return eval_unary(first_literal.view(), operand.view());
    }

    /* A binary primary, with the operator either a < or > angle or a word
       operator such as == or -eq. */
    if (pos + 1 < elements.count()) {
      const Kind next = kind_at(pos + 1);
      if (next == Kind::Less || next == Kind::Greater) {
        if (pos + 2 >= elements.count() || kind_at(pos + 2) != Kind::Operand)
          throw Error{"Unable to evaluate the [[ ]] because an operand is "
                      "missing after a comparison"};
        pos += 3;
        if (is_skipping) return false;
        const String left = operand_value(elements[pos - 3]);
        const String right = operand_value(elements[pos - 1]);
        return next == Kind::Less ? left < right : right < left;
      }
      if (next == Kind::Operand) {
        const String op = operand_literal(elements[pos + 1]);
        if (is_binary_word_op(op.view())) {
          if (pos + 2 >= elements.count() || kind_at(pos + 2) != Kind::Operand)
            throw Error{"[[: expected operand after '" + op + "'"};
          pos += 3;
          if (is_skipping) return false;
          const String left = operand_value(elements[pos - 3]);
          /* == and != glob-match, and =~ regex-matches, with a quoting mask, so
             a quoted metacharacter of the right operand matches literally. The
             other binary operators read a plain string right operand. */
          if (op == "==" || op == "=" || op == "!=") {
            let active = ArrayList<bool>{cxt.scratch_allocator()};
            const String pattern =
                operand_pattern_masked(elements[pos - 1], active);
            const bool matched = utils::glob_matches(
                pattern.view(), left.view(), active, 0, cxt.extglob_enabled());
            return op == "!=" ? !matched : matched;
          }
          if (op == "=~") {
            let active = ArrayList<bool>{cxt.scratch_allocator()};
            const String pattern =
                operand_pattern_masked(elements[pos - 1], active);
            return regex_match(left.view(), pattern.view(), active);
          }
          const String right = operand_value(elements[pos - 1]);
          return eval_binary(left.view(), op.view(), right.view());
        }
      }
    }

    /* A lone operand is true when it is non-empty. */
    pos++;
    if (is_skipping) return false;
    const String value = operand_value(elements[pos - 1]);
    return !value.is_empty();
  }

  bool eval_term() throws
  {
    if (!at_end() && kind_at(pos) == Kind::Not) {
      pos++;
      return !eval_term();
    }
    if (!at_end() && kind_at(pos) == Kind::OpenParen) {
      pos++;
      const bool inner = eval_or();
      if (at_end() || kind_at(pos) != Kind::CloseParen)
        throw Error{"[[: expected ')'"};
      pos++;
      return inner;
    }
    return eval_primary();
  }

  bool eval_and() throws
  {
    bool result = eval_term();
    while (!at_end() && kind_at(pos) == Kind::And) {
      pos++;
      /* A false left already decides the and, so the right is parsed without
         evaluation. The skip nests, so an outer skip stays set. */
      const bool was_skipping = is_skipping;
      is_skipping = is_skipping || !result;
      const bool rhs = eval_term();
      is_skipping = was_skipping;
      result = result && rhs;
    }
    return result;
  }

  bool eval_or() throws
  {
    bool result = eval_and();
    while (!at_end() && kind_at(pos) == Kind::Or) {
      pos++;
      /* A true left already decides the or, so the right is parsed without
         evaluation. */
      const bool was_skipping = is_skipping;
      is_skipping = is_skipping || result;
      const bool rhs = eval_and();
      is_skipping = was_skipping;
      result = result || rhs;
    }
    return result;
  }
};

} /* namespace */

#if SHIT_PLATFORM_IS POSIX
/* The most distinct regex patterns the cache holds before it is cleared, so a
   pathological loop that builds a fresh pattern every iteration stays bounded
   rather than growing the table without end. A real script reuses a handful. */
static constexpr usize REGEX_CACHE_CAP = 128;

fn EvalContext::cached_compiled_regex(StringView pattern) throws -> regex_t *
{
  /* The key is the pattern text alone, which is sound only because compilation
     depends on nothing else, the flags are always REG_EXTENDED. A future option
     that changes compilation, such as REG_ICASE for nocasematch, must fold into
     the key so two intended compilations of one pattern do not collide. */
  if (CompiledRegex *cached = m_regex_cache.find(pattern)) {
    LOG(All, "regex cache hit for the pattern '%.*s'",
        static_cast<int>(pattern.length), pattern.data);
    return cached->get();
  }

  /* A bounded miss path. When the cache is full it is cleared whole, which
     frees every compiled entry, rather than tracking a per-entry age. */
  if (m_regex_cache.count() >= REGEX_CACHE_CAP) {
    LOG(Debug, "regex cache full, dropping %zu compiled patterns",
        m_regex_cache.count());
    m_regex_cache.clear();
  }

  LOG(Debug, "regex cache miss, compiling the pattern '%.*s'",
      static_cast<int>(pattern.length), pattern.data);
  let const pattern_text = String{scratch_allocator(), pattern};
  regex_t compiled;
  if (regcomp(&compiled, pattern_text.c_str(), REG_EXTENDED) != 0) {
    /* bash returns status 2 for a malformed regex, which the conditional turns
       into an evaluation error. */
    throw Error{"Unable to evaluate the [[ ]] because the regular expression "
                "is invalid"};
  }
  m_regex_cache.set(pattern, CompiledRegex{compiled});
  return m_regex_cache.find(pattern)->get();
}
#endif

fn EvalContext::evaluate_conditional(
    const ArrayList<conditional_element> &elements) throws -> bool
{
  if (elements.is_empty())
    throw Error{"Unable to evaluate the [[ ]] because the conditional "
                "expression is empty"};
  LOG(Debug, "evaluating a [[ ]] conditional of %zu elements",
      elements.count());
  let evaluator = ConditionalEvaluator{*this, elements};
  const bool result = evaluator.eval_or();
  if (!evaluator.at_end())
    throw Error{
        "Unable to evaluate the [[ ]] because the token '" +
        evaluator.unexpected_token() +
        "' came after a complete conditional, so it may be an operator shit "
        "does not support or a missing && or || between two tests"};
  return result;
}

} /* namespace shit */
