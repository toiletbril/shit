#include "Builtin.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* POSIX regcomp and regexec back the [[ =~ operator, since the release build
   drops libstdc++ and std::regex with it. */
#if SHIT_PLATFORM_IS POSIX
#include <regex.h>
#endif

#if SHIT_PLATFORM_IS WIN32
#include <io.h>
#endif

namespace shit {

cold [[noreturn]] static fn fail_conditional(StringView message,
                                             StringView reason) throws -> void
{
  let error = Error{message};
  error.set_note(reason);
  throw error;
}

cold [[noreturn]] static fn fail_conditional(StringView reason) throws -> void
{
  fail_conditional("Unable to evaluate the [[ ]]", reason);
}

namespace {

struct conditional_evaluator
{
  EvalContext &cxt;
  const ArrayList<conditional_element> &elements;
  usize pos = 0;
  /* A decided && or || branch is parsed to advance past its tokens but not
     evaluated, so a dead-branch glob, regex, or command substitution runs no
     side effect and raises no error, the way bash short-circuits [[ ]]. */
  bool is_skipping = false;

  using Kind = conditional_element::Kind;

  pure fn at_end() const wontthrow -> bool { return pos >= elements.count(); }
  pure fn kind_at(usize i) const wontthrow -> Kind { return elements[i].kind; }

  fn unexpected_token() throws -> String
  {
    return at_end() ? String{heap_allocator()} : operand_literal(elements[pos]);
  }

  fn operand_literal(const conditional_element &e) throws -> String
  {
    if (e.word != nullptr && e.word->kind() == Token::Kind::Word) {
      return static_cast<const tokens::WordToken *>(e.word)
          ->word()
          .to_literal_string();
    }
    if (e.word != nullptr) return e.word->raw_string();
    return String{heap_allocator()};
  }

  fn operand_value(const conditional_element &e) throws -> String
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

  /* The mask marks which *, ?, and [ stay active. A quoted or escaped
     metacharacter is masked off and matches literally. */
  fn operand_pattern_masked(const conditional_element &e,
                            Bitset &active) throws -> String
  {
    if (e.word != nullptr && e.word->kind() == Token::Kind::Word) {
      try {
        return cxt.expand_case_pattern_masked(
            static_cast<const tokens::WordToken *>(e.word)->word(), active);
      } catch (const Error &err) {
        throw relocate_error(err, e.word->source_location());
      }
    }
    let raw =
        e.word != nullptr ? e.word->raw_string() : String{heap_allocator()};
    for (usize i = 0; i < raw.count(); i++)
      active.push(true);
    return raw;
  }

  static pure fn is_unary_op(StringView s) wontthrow -> bool
  {
    static constexpr StaticStringMap<bool>::entry ENTRIES[] = {
        {SSK("-z"), true},
        {SSK("-n"), true},
        {SSK("-e"), true},
        {SSK("-f"), true},
        {SSK("-d"), true},
        {SSK("-r"), true},
        {SSK("-w"), true},
        {SSK("-x"), true},
        {SSK("-s"), true},
        {SSK("-h"), true},
        {SSK("-L"), true},
        {SSK("-b"), true},
        {SSK("-c"), true},
        {SSK("-p"), true},
        {SSK("-S"), true},
        {SSK("-g"), true},
        {SSK("-u"), true},
        {SSK("-k"), true},
        {SSK("-O"), true},
        {SSK("-G"), true},
        {SSK("-v"), true},
        {SSK("-t"), true},
        {SSK("-o"), true},
    };
    static constexpr StaticStringMap<bool> UNARY_OPS{ENTRIES, countof(ENTRIES)};
    return UNARY_OPS.find(s).has_value();
  }

  static pure fn is_binary_word_op(StringView s) wontthrow -> bool
  {
    static constexpr StaticStringMap<bool>::entry ENTRIES[] = {
        {SSK("="),   true},
        {SSK("=="),  true},
        {SSK("!="),  true},
        {SSK("=~"),  true},
        {SSK("-eq"), true},
        {SSK("-ne"), true},
        {SSK("-lt"), true},
        {SSK("-le"), true},
        {SSK("-gt"), true},
        {SSK("-ge"), true},
        {SSK("-ef"), true},
        {SSK("-nt"), true},
        {SSK("-ot"), true},
    };
    static constexpr StaticStringMap<bool> BINARY_WORD_OPS{ENTRIES,
                                                           countof(ENTRIES)};
    return BINARY_WORD_OPS.find(s).has_value();
  }

  static pure fn is_regex_metacharacter(char c) wontthrow -> bool
  {
    return c == '.' || c == '^' || c == '$' || c == '*' || c == '+' ||
           c == '?' || c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '|' || c == '\\';
  }

  fn regex_match(StringView value, StringView pattern,
                 const Bitset &active) throws -> bool
  {
#if SHIT_PLATFORM_IS POSIX
    /* An inactive mask byte came from a quoted part of the operand, so a regex
       metacharacter there is backslash-escaped to match itself. */
    let escaped_pattern = String{cxt.scratch_allocator()};
    for (usize i = 0; i < pattern.length; i++) {
      const bool is_literal = i < active.count() && !active[i];
      if (is_literal && is_regex_metacharacter(pattern[i])) {
        escaped_pattern += '\\';
      }
      escaped_pattern += pattern[i];
    }
    regex_t *compiled = cxt.cached_compiled_regex(escaped_pattern.view());
    let const value_text = String{cxt.scratch_allocator(), value};
    let const group_count = compiled->re_nsub + 1;
    let matches = ArrayList<regmatch_t>{cxt.scratch_allocator()};
    matches.reserve(group_count);
    for (usize i = 0; i < group_count; i++)
      matches.push(regmatch_t{});
    const int match_result =
        regexec(compiled, value_text.c_str(), group_count, matches.begin(), 0);
    LOG(All, "the =~ regex %s the value",
        match_result == 0 ? "matched" : "did not match");
    if (match_result == REG_NOMATCH) {
      cxt.set_indexed_array("BASH_REMATCH",
                            ArrayList<String>{heap_allocator()});
      return false;
    }
    if (match_result != 0) {
      /* A genuine engine failure such as REG_ESPACE surfaces with the engine's
         own message instead of reading as false. */
      char error_text[256];
      regerror(match_result, compiled, error_text, sizeof(error_text));
      fail_conditional("Unable to match the =~ pattern", error_text);
    }

    let rematch = ArrayList<String>{heap_allocator()};
    rematch.reserve(group_count);
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
    fail_conditional("Unable to use =~ in the [[ ]]",
                     "It is not supported on this platform");
#endif
  }

  fn eval_unary(StringView op, StringView operand) throws -> bool
  {
    if (op == "-z") return operand.is_empty();
    if (op == "-n") return !operand.is_empty();
    if (op == "-v") {
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
    if (op == "-t") {
      if (ErrorOr<i64> descriptor = operand.to<i64>();
          !descriptor.is_error())
#if SHIT_PLATFORM_IS WIN32
        return os::is_fd_a_tty(reinterpret_cast<os::descriptor>(
            _get_osfhandle(static_cast<int>(descriptor.value()))));
#else
        return os::is_fd_a_tty(static_cast<os::descriptor>(descriptor.value()));
#endif
      /* bash reports a non-integer -t operand with status 2. */
      let error = Error{"Unable to test '-t " + operand + "'"};
      error.set_note("The operand is not an integer");
      error.set_command_status(2);
      throw error;
    }
    if (op == "-h" || op == "-L") return path.is_symbolic_link();
    if (op == "-b") return path.is_block_device();
    if (op == "-c") return path.is_character_device();
    if (op == "-p") return path.is_fifo();
    if (op == "-S") return path.is_socket();
    if (op == "-g") return path.has_setgid_bit();
    if (op == "-u") return path.has_setuid_bit();
    if (op == "-k") return path.has_sticky_bit();
    if (op == "-O") return path.is_owned_by_effective_user();
    if (op == "-G") return path.is_owned_by_effective_group();
    /* The emacs line-editing pseudo-option has no set toggle, so it answers
       on. An unknown name reads off. */
    if (op == "-o") {
      if (operand == "emacs") return true;
      return query_shell_option(cxt, operand).value_or(false);
    }
    return path.exists();
  }

  fn eval_binary(StringView left, StringView op, StringView right) throws
      -> bool
  {
    if (op == "-ef") return Path{left}.is_same_file_as(Path{right});
    if (op == "-nt") return Path{left}.is_newer_than(Path{right});
    if (op == "-ot") return Path{left}.is_older_than(Path{right});

    /* The arithmetic comparison operands are full expressions, so 1+1 and a
       bare variable name evaluate. An empty operand reads as zero. */
    let const do_to_number = [&](StringView operand) throws -> i64 {
      for (usize i = 0; i < operand.length; i++) {
        if (operand[i] != ' ' && operand[i] != '\t') {
          return cxt.evaluate_arithmetic(operand);
        }
      }
      return 0;
    };
    const i64 left_number = do_to_number(left);
    const i64 right_number = do_to_number(right);
    if (op == "-eq") return left_number == right_number;
    if (op == "-ne") return left_number != right_number;
    if (op == "-lt") return left_number < right_number;
    if (op == "-le") return left_number <= right_number;
    if (op == "-gt") return left_number > right_number;
    return left_number >= right_number;
  }

  fn eval_primary() throws -> bool
  {
    if (at_end()) fail_conditional("The expression ends unexpectedly");
    const conditional_element &first = elements[pos];
    if (first.kind != Kind::Operand)
      fail_conditional("An operator appears where an operand is expected");

    const String first_literal = operand_literal(first);

    if (is_unary_op(first_literal.view()) && pos + 1 < elements.count() &&
        kind_at(pos + 1) == Kind::Operand)
    {
      pos += 2;
      if (is_skipping) return false;
      /* bash does not nounset the operand of -v, so the unset-variable
         diagnostic stays silent while it expands. The defer restores the prior
         value so a throw cannot strand the suppression on. */
      let const is_existence_test = first_literal.view() == "-v";
      let const saved_suppress_unset =
          cxt.is_warning_suppressed(suppressible_warning::UnsetReference);
      let const saved_suppress_test_operand =
          cxt.is_warning_suppressed(suppressible_warning::UnsetTestOperand);
      cxt.set_warning_suppressed(suppressible_warning::UnsetTestOperand, true);
      if (is_existence_test)
        cxt.set_warning_suppressed(suppressible_warning::UnsetReference, true);
      defer
      {
        cxt.set_warning_suppressed(suppressible_warning::UnsetReference,
                                   saved_suppress_unset);
        cxt.set_warning_suppressed(suppressible_warning::UnsetTestOperand,
                                   saved_suppress_test_operand);
      };
      const String operand = operand_value(elements[pos - 1]);
      return eval_unary(first_literal.view(), operand.view());
    }

    if (pos + 1 < elements.count()) {
      const Kind next = kind_at(pos + 1);
      if (next == Kind::Less || next == Kind::Greater) {
        if (pos + 2 >= elements.count() || kind_at(pos + 2) != Kind::Operand)
          fail_conditional("An operand is missing after a comparison");
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
            throw Error{"Expected an operand after '" + op + "'"};
          pos += 3;
          if (is_skipping) return false;
          const String left = operand_value(elements[pos - 3]);
          if (op == "==" || op == "=" || op == "!=") {
            let active = Bitset{cxt.scratch_allocator()};
            const String pattern =
                operand_pattern_masked(elements[pos - 1], active);
            const bool is_matched = utils::glob_matches(
                pattern.view(), left.view(), active, 0, cxt.extglob_enabled());
            return op == "!=" ? !is_matched : is_matched;
          }
          if (op == "=~") {
            let active = Bitset{cxt.scratch_allocator()};
            const String pattern =
                operand_pattern_masked(elements[pos - 1], active);
            /* A malformed regex throws without a location, so the caret is
               pointed at the regex operand. */
            try {
              return regex_match(left.view(), pattern.view(), active);
            } catch (const Error &err) {
              const conditional_element &operand = elements[pos - 1];
              if (operand.word != nullptr)
                throw relocate_error(err, operand.word->source_location());
              throw;
            }
          }
          const String right = operand_value(elements[pos - 1]);
          return eval_binary(left.view(), op.view(), right.view());
        }
      }
    }

    pos++;
    if (is_skipping) return false;
    const String value = operand_value(elements[pos - 1]);
    return !value.is_empty();
  }

  fn eval_term() throws -> bool
  {
    if (!at_end() && kind_at(pos) == Kind::Not) {
      pos++;
      return !eval_term();
    }
    if (!at_end() && kind_at(pos) == Kind::OpenParen) {
      pos++;
      const bool is_inner_true = eval_or();
      if (at_end() || kind_at(pos) != Kind::CloseParen)
        throw Error{"Expected ')'"};
      pos++;
      return is_inner_true;
    }
    return eval_primary();
  }

  fn eval_and() throws -> bool
  {
    let and_result = eval_term();
    while (!at_end() && kind_at(pos) == Kind::And) {
      pos++;
      const bool was_skipping = is_skipping;
      is_skipping = is_skipping || !and_result;
      const bool rhs = eval_term();
      is_skipping = was_skipping;
      and_result = and_result && rhs;
    }
    return and_result;
  }

  fn eval_or() throws -> bool
  {
    let or_result = eval_and();
    while (!at_end() && kind_at(pos) == Kind::Or) {
      pos++;
      const bool was_skipping = is_skipping;
      is_skipping = is_skipping || or_result;
      const bool rhs = eval_and();
      is_skipping = was_skipping;
      or_result = or_result || rhs;
    }
    return or_result;
  }
};

} // namespace

#if SHIT_PLATFORM_IS POSIX
static constexpr usize REGEX_CACHE_CAP = 128;

fn EvalContext::cached_compiled_regex(StringView pattern) throws -> regex_t *
{
  /* The key is the pattern text alone, sound only because the flags are always
     REG_EXTENDED. A future option that changes compilation, such as REG_ICASE
     for nocasematch, must fold into the key or two compilations collide. */
  if (CompiledRegex *cached = m_regex_cache.find(pattern); cached != nullptr) {
    LOG(All, "regex cache hit for the pattern '%.*s'",
        static_cast<int>(pattern.length), pattern.data);
    return cached->get();
  }

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
    let reason = String{scratch_allocator()};
    reason += "The regular expression '";
    reason += pattern;
    reason += "' is invalid";
    fail_conditional(reason.view());
  }
  m_regex_cache.set(pattern, CompiledRegex{compiled});
  return m_regex_cache.find(pattern)->get();
}
#endif

fn EvalContext::evaluate_conditional(
    const ArrayList<conditional_element> &elements) throws -> bool
{
  if (elements.is_empty())
    fail_conditional("The conditional expression is empty");
  LOG(Debug, "evaluating a [[ ]] conditional of %zu elements",
      elements.count());
  let evaluator = conditional_evaluator{*this, elements};
  const bool is_conditional_true = evaluator.eval_or();
  if (!evaluator.at_end()) {
    fail_conditional("The token '" + evaluator.unexpected_token() +
                     "' came after a complete conditional, so it may be an "
                     "operator shit does not support or a missing && or || "
                     "between two tests");
  }
  return is_conditional_true;
}

} // namespace shit
