#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-W wordlist] [-G glob] [-A action] [-P prefix] "
                   "[-S suffix] [-X filterpat] [-F function] [-C command] "
                   "[word]");
HELP_DESCRIPTION_DECL(
    "The compgen builtin writes the completion candidates for a word.");

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
      if (argument == "-A" || argument == "-P" || argument == "-S" ||
          argument == "-X" || argument == "-F" || argument == "-C")
        i++;
      i++;
      continue;
    }
    word = argument;
    i++;
  }

  if (glob_pattern.has_value()) {
    LOG(All, "compgen expanding glob '%.*s' for prefix '%.*s'",
        static_cast<int>(glob_pattern->length), glob_pattern->data,
        static_cast<int>(word.length), word.data);
    let out = String{cxt.scratch_allocator()};
    let has_any_matched = false;
    for (let const &match : cxt.expand_glob_lenient(*glob_pattern)) {
      if (!match.view().starts_with(word)) continue;
      out.append(match.view());
      out.push('\n');
      has_any_matched = true;
    }

    if (has_any_matched) ec.print_to_stdout(out.view());
    return has_any_matched ? 0 : 1;
  }

  if (!wordlist.has_value()) return 1;

  LOG(Debug, "compgen filtering word list for prefix '%.*s'",
      static_cast<int>(word.length), word.data);

  let out = String{cxt.scratch_allocator()};
  let has_any_matched = false;
  for (let const &candidate : cxt.expand_wordlist_to_fields(*wordlist)) {
    if (candidate.is_empty() || !candidate.view().starts_with(word)) continue;
    out.append(candidate.view());
    out.push('\n');
    has_any_matched = true;
  }

  ec.print_to_stdout(out.view());
  return has_any_matched ? 0 : 1;
}

} // namespace shit
