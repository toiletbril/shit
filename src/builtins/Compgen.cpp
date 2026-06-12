#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* compgen generates the completion candidates an action or a word list would
   produce, the way a bash completion function does, such as compgen -W "a b c"
   -- "$cur". The -W word list is the common form, split on whitespace and
   filtered to the words that start with the given prefix, each printed on its
   own line. The other options are accepted so a completion script runs, though
   only the word list yields candidates for now. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-W wordlist] [-A action] [options] [word]");
HELP_DESCRIPTION_DECL(
    "The compgen builtin writes the completion candidates for the given "
    "options "
    "and word to standard output. The -W word list is split on whitespace and "
    "filtered to the entries that start with the word, each on its own line. "
    "The other options are accepted so a completion script runs, though only "
    "the word list yields candidates for now.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Compgen::Compgen() = default;

pure fn Compgen::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Compgen;
}

fn Compgen::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(cxt);
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  Maybe<StringView> wordlist;
  Maybe<StringView> glob_pattern;
  StringView word{};
  for (usize i = 1; i < args.count();) {
    let const argument = args[i].view();
    if (argument == "--") {
      i++;
      if (i < args.count()) word = args[i].view();
      break;
    }
    if (argument == "-W") {
      i++;
      if (i < args.count()) {
        wordlist = args[i].view();
        i++;
      }
      continue;
    }
    if (argument == "-G") {
      i++;
      if (i < args.count()) {
        glob_pattern = args[i].view();
        i++;
      }
      continue;
    }
    if (argument.length >= 2 && argument[0] == '-') {
      /* These options carry a value, so the value argument is skipped too. The
         action-letter options such as -c or -f carry none. */
      if (argument == "-A" || argument == "-P" || argument == "-S" ||
          argument == "-X" || argument == "-F" || argument == "-C")
        i++;
      i++;
      continue;
    }
    word = argument;
    i++;
  }

  /* compgen -G expands the pattern against the filesystem with failglob
     suppressed, the probe the strict mood's unmatched-glob error points at.
     The matches print one per line and the status reports whether any matched,
     so 'if compgen -G pat >/dev/null' reads as an existence test. */
  if (glob_pattern.has_value()) {
    LOG(verbosity::All, "compgen expanding glob '%.*s' for prefix '%.*s'",
        static_cast<int>(glob_pattern->length), glob_pattern->data,
        static_cast<int>(word.length), word.data);
    let out = String{};
    let any_matched = false;
    for (const String &match : cxt.expand_glob_lenient(*glob_pattern)) {
      if (word.length != 0 && !match.view().starts_with(word)) continue;
      out.append(match.view());
      out.push('\n');
      any_matched = true;
    }
    if (any_matched) ec.print_to_stdout(out.view());
    return any_matched ? 0 : 1;
  }

  /* An unsupported action produces nothing rather than an error, so a
     completion script that asks for one keeps running with an empty result. */
  if (!wordlist.has_value()) return 1;

  LOG(verbosity::Debug, "compgen filtering word list for prefix '%.*s'",
      static_cast<int>(word.length), word.data);

  /* The -W list undergoes the shell expansions bash applies to it through
     the shared word-list expander, the same path complete -W reads, so the
     bash-completion idiom -W '"${options[@]}"' reaches the caller's array. */
  let out = String{};
  let any_matched = false;
  for (const String &candidate : cxt.expand_wordlist_to_fields(*wordlist)) {
    if (candidate.is_empty() || !candidate.view().starts_with(word)) continue;
    out.append(candidate.view());
    out.push('\n');
    any_matched = true;
  }

  ec.print_to_stdout(out.view());
  return any_matched ? 0 : 1;
}

} /* namespace shit */
