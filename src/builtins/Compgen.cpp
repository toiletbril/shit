#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* compgen generates the completion candidates an action, a word list, or a
   glob would produce, the way a bash completion function does, such as
   compgen -W "a b c" -- "$cur". The -W word list undergoes the shell
   expansions and filters to the words that start with the given prefix, each
   printed on its own line, and -G probes the filesystem. The other options
   are accepted so a completion script runs. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-W wordlist] [-G glob] [-A action] [-P prefix] "
                   "[-S suffix] [-X filterpat] [-F function] [-C command] "
                   "[word]");
HELP_DESCRIPTION_DECL(
    "The compgen builtin writes the completion candidates for the given "
    "options and word to standard output, each on its own line.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
/* The value-carrying options are hand-parsed in execute, so these FLAG rows
   only feed the help text. */
FLAG(COMPGEN_WORDLIST, String, 'W', "",
     "Expand the word list the way the shell does and filter to the entries "
     "that start with the word.");
FLAG(COMPGEN_GLOB, String, 'G', "", "Probe the filesystem with the glob.");
FLAG(COMPGEN_ACTION, String, 'A', "", "Accepted without effect.");
FLAG(COMPGEN_PREFIX, String, 'P', "", "Accepted without effect.");
FLAG(COMPGEN_SUFFIX, String, 'S', "", "Accepted without effect.");
FLAG(COMPGEN_FILTER, String, 'X', "", "Accepted without effect.");
FLAG(COMPGEN_FUNCTION, String, 'F', "", "Accepted without effect.");
FLAG(COMPGEN_COMMAND, String, 'C', "", "Accepted without effect.");

REGISTER_BUILTIN_FLAGS(Compgen);

namespace shit {

Compgen::Compgen() = default;

pure fn Compgen::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Compgen;
}

fn Compgen::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
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
