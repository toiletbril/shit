#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Completion.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-c] [-W wordlist] [-G glob] [-A action] [-P prefix] "
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
FLAG(COMPGEN_FILE, Bool, 'f', "", "List matching filenames.");
FLAG(COMPGEN_ACTION, String, 'A', "", "List commands for the command action.");
FLAG(COMPGEN_PREFIX, String, 'P', "", "Accepted without effect.");
FLAG(COMPGEN_SUFFIX, String, 'S', "", "Accepted without effect.");
FLAG(COMPGEN_FILTER, String, 'X', "",
     "Remove matching candidates, with leading ! reversing the filter and "
     "unescaped & expanding to the completion word.");
FLAG(COMPGEN_FUNCTION, String, 'F', "", "Accepted without effect.");
FLAG(COMPGEN_COMMAND, String, 'C', "", "Accepted without effect.");

REGISTER_BUILTIN_FLAGS(Compgen);

namespace shit {

Compgen::Compgen() = default;

struct compgen_filter
{
  explicit compgen_filter(Allocator allocator)
      : pattern(allocator), active(allocator)
  {}

  String pattern;
  Bitset active;
  bool is_negated{false};
};

static fn compile_filter(StringView raw_filter, StringView word,
                         Allocator allocator) throws -> compgen_filter
{
  let compiled = compgen_filter{allocator};
  usize position = 0;
  if (!raw_filter.is_empty() && raw_filter[0] == '!') {
    compiled.is_negated = true;
    position++;
  }

  while (position < raw_filter.length) {
    let const character = raw_filter[position++];
    if (character == '\\' && position < raw_filter.length) {
      compiled.pattern.push(raw_filter[position++]);
      compiled.active.push(false);
      continue;
    }
    if (character == '&') {
      compiled.pattern.append(word);
      for (usize i = 0; i < word.length; i++)
        compiled.active.push(false);
      continue;
    }
    compiled.pattern.push(character);
    compiled.active.push(character != '\\');
  }
  return compiled;
}

static fn candidate_is_excluded(StringView candidate,
                                const compgen_filter &filter,
                                const EvalContext &cxt) throws -> bool
{
  let const matches =
      utils::glob_matches(filter.pattern.view(), candidate, filter.active, 0,
                          cxt.extglob_enabled());
  return filter.is_negated ? !matches : matches;
}

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
  Maybe<StringView> action;
  Maybe<StringView> filter_pattern;
  bool should_list_commands = false;
  bool should_list_files = false;
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
    if (argument == "-A") {
      i++;
      if (i < args.count()) {
        action = args[i].view();
        i++;
      }
      continue;
    }
    if (argument == "-X") {
      i++;
      if (i < args.count()) {
        filter_pattern = args[i].view();
        i++;
      }
      continue;
    }
    if (argument == "-c") {
      should_list_commands = true;
      i++;
      continue;
    }
    if (argument == "-f") {
      should_list_files = true;
      i++;
      continue;
    }
    if (argument.length >= 2 && argument[0] == '-') {
      if (argument == "-P" || argument == "-S" || argument == "-F" ||
          argument == "-C" || argument == "-o")
      {
        i++;
      }
      i++;
      continue;
    }
    word = argument;
    i++;
  }

  Maybe<compgen_filter> filter = None;
  if (filter_pattern.has_value())
    filter = compile_filter(*filter_pattern, word, cxt.scratch_allocator());

  if (should_list_commands || (action.has_value() && *action == "command")) {
    cxt.get_program_resolver().begin_explicit_completion(
        ProgramResolver::CompletionRefresh::Fresh);
    defer { cxt.get_program_resolver().end_explicit_completion(); };
    let out = String{cxt.scratch_allocator()};
    let has_any_matched = false;
    for (let const &candidate : completion::complete_command_names(
             word, completion::command_match_mode::Prefix, cxt))
    {
      if (filter.has_value() &&
          candidate_is_excluded(candidate.view(), *filter, cxt))
        continue;
      out.append(candidate.view());
      out.push('\n');
      has_any_matched = true;
    }

    if (has_any_matched) ec.print_to_stdout(out.view());
    return has_any_matched ? 0 : 1;
  }

  if (glob_pattern.has_value()) {
    LOG(All, "compgen expanding glob '%.*s' for prefix '%.*s'",
        static_cast<int>(glob_pattern->length), glob_pattern->data,
        static_cast<int>(word.length), word.data);
    let out = String{cxt.scratch_allocator()};
    let has_any_matched = false;
    for (let const &match : cxt.expand_glob_lenient(*glob_pattern)) {
      if (!match.view().starts_with(word)) continue;
      if (filter.has_value() &&
          candidate_is_excluded(match.view(), *filter, cxt))
        continue;
      out.append(match.view());
      out.push('\n');
      has_any_matched = true;
    }

    if (has_any_matched) ec.print_to_stdout(out.view());
    return has_any_matched ? 0 : 1;
  }

  if (should_list_files) {
    cxt.get_program_resolver().begin_explicit_completion(
        ProgramResolver::CompletionRefresh::Fresh);
    defer { cxt.get_program_resolver().end_explicit_completion(); };
    let const candidates = completion::complete_filesystem_names(
        word, cxt, Path::current_directory());
    let out = String{cxt.scratch_allocator()};
    for (let const &candidate : candidates) {
      if (filter.has_value() &&
          candidate_is_excluded(candidate.view(), *filter, cxt))
        continue;
      out.append(candidate.view());
      out.push('\n');
    }
    if (!out.is_empty()) ec.print_to_stdout(out.view());
    return out.is_empty() ? 1 : 0;
  }

  if (!wordlist.has_value()) return 1;

  LOG(Debug, "compgen filtering word list for prefix '%.*s'",
      static_cast<int>(word.length), word.data);

  let out = String{cxt.scratch_allocator()};
  let has_any_matched = false;
  for (let const &candidate : cxt.expand_wordlist_to_fields(*wordlist)) {
    if (!candidate.view().starts_with(word)) continue;
    if (filter.has_value() &&
        candidate_is_excluded(candidate.view(), *filter, cxt))
      continue;
    out.append(candidate.view());
    out.push('\n');
    has_any_matched = true;
  }

  ec.print_to_stdout(out.view());
  return has_any_matched ? 0 : 1;
}

} /* namespace shit */
