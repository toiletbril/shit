#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

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

  StringView wordlist{};
  let have_wordlist = false;
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
        have_wordlist = true;
        i++;
      }
      continue;
    }
    if (argument.length >= 2 && argument[0] == '-') {
      /* These options carry a value, so the value argument is skipped too. The
         action-letter options such as -c or -f carry none. */
      if (argument == "-A" || argument == "-G" || argument == "-P" ||
          argument == "-S" || argument == "-X" || argument == "-F" ||
          argument == "-C")
        i++;
      i++;
      continue;
    }
    word = argument;
    i++;
  }

  /* An unsupported action produces nothing rather than an error, so a
     completion script that asks for one keeps running with an empty result. */
  if (!have_wordlist) return 1;

  let out = String{};
  let any_matched = false;
  let const emit = [&](StringView candidate) {
    if (candidate.length == 0) return;
    if (word.length == 0 || candidate.starts_with(word)) {
      out.append(candidate);
      out.push('\n');
      any_matched = true;
    }
  };

  /* The word list splits on the ASCII whitespace bash treats as the default
     field separators. A trailing sentinel flushes the final word. */
  usize start = 0;
  for (usize i = 0; i <= wordlist.length; i++) {
    let const c = (i < wordlist.length) ? wordlist[i] : ' ';
    if (c == ' ' || c == '\t' || c == '\n') {
      if (i > start) emit(wordlist.substring_of_length(start, i - start));
      start = i + 1;
    }
  }

  ec.print_to_stdout(out.view());
  return any_matched ? 0 : 1;
}

} /* namespace shit */
