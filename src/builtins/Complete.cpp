#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abcdefgjksuv] [-o option] [-A action] [-G globpat] "
                   "[-W wordlist] [-F function] [-C command] [-X filterpat] "
                   "[-P prefix] [-S suffix] [-pr] [name ...]");
HELP_DESCRIPTION_DECL(
    "The complete builtin registers a completion spec for a command.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
/* The options are hand-parsed in execute since several only skip their
   value, so these FLAG rows only feed the help text. */
FLAG(COMPLETE_WORDLIST, String, 'W', "",
     "Register the word list as the command's candidates.");
FLAG(COMPLETE_FUNCTION, String, 'F', "",
     "Register the function to run on an explicit tab, COMPREPLY style.");
FLAG(COMPLETE_OPTION, String, 'o', "",
     "default, bashdefault, and dirnames fall back to filename completion, "
     "any other option is accepted without effect.");
FLAG(COMPLETE_PRINT, Bool, 'p', "",
     "Print the named specs, or every spec, in a replayable form.");
FLAG(COMPLETE_DEFAULT, Bool, 'D', "",
     "Register the default spec used for a command with no spec of its own.");
FLAG(COMPLETE_REMOVE, Bool, 'r', "", "Accepted without effect.");

REGISTER_BUILTIN_FLAGS(Complete);

namespace shit {

Complete::Complete() = default;

pure fn Complete::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Complete;
}

fn Complete::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let function_name = String{cxt.scratch_allocator()};
  let word_list = String{cxt.scratch_allocator()};
  let should_use_default = false;
  let is_default_completion = false;
  let should_print_specs = false;
  let commands = ArrayList<String>{cxt.scratch_allocator()};

  for (usize i = 1; i < args.count();) {
    let const arg = args[i].view();
    if (arg == "--") {
      for (i++; i < args.count(); i++)
        commands.push_managed(args[i].view());
      break;
    }
    if (arg == "-p") {
      should_print_specs = true;
      i++;
      continue;
    }
    if (arg == "-D") {
      is_default_completion = true;
      i++;
      continue;
    }
    if (arg == "-F") {
      if (++i < args.count()) function_name = String{heap_allocator(), args[i]};
      i++;
      continue;
    }
    if (arg == "-W") {
      if (++i < args.count()) word_list = String{heap_allocator(), args[i]};
      i++;
      continue;
    }
    if (arg == "-o") {
      if (++i < args.count() &&
          (args[i] == "default" || args[i] == "bashdefault" ||
           args[i] == "dirnames"))
      {
        should_use_default = true;
      }
      i++;
      continue;
    }
    if (arg == "-A" || arg == "-G" || arg == "-C" || arg == "-X" ||
        arg == "-P" || arg == "-S")
    {
      i += 2;
      continue;
    }
    if (!arg.is_empty() && arg[0] == '-') {
      i++;
      continue;
    }
    commands.push_managed(arg);
    i++;
  }

  /* -p reports failure when a named command has no spec, since the
     bash-completion loader reads a successful print as a registered spec. */
  if (should_print_specs) {
    let const do_print_one_spec = [&](StringView command,
                                      const completion_spec &spec) throws {
      let line = String{cxt.scratch_allocator(), "complete "};
      if (spec.should_use_default) line += "-o default ";
      if (!spec.function_name.is_empty()) {
        line += "-F ";
        line += spec.function_name.view();
        line += ' ';
      }
      if (!spec.word_list.is_empty()) {
        /* Each embedded quote is written as the '\'' escape so a list carrying
           an apostrophe replays as valid shell. */
        line += "-W '";
        const StringView list = spec.word_list.view();
        for (usize i = 0; i < list.length; i++) {
          if (list[i] == '\'')
            line += "'\\''";
          else
            line.push(list[i]);
        }
        line += "' ";
      }
      line += command;
      line += '\n';
      ec.print_to_stdout(line.view());
    };
    if (commands.is_empty()) {
      cxt.completion_specs().for_each(
          [&](StringView command, const completion_spec &spec) {
            do_print_one_spec(command, spec);
          });
      return 0;
    }

    i32 print_status = 0;
    for (let const &command : commands) {
      const completion_spec *spec = cxt.lookup_completion_spec(command.view());
      if (spec == nullptr) {
        print_status = 1;
        continue;
      }
      do_print_one_spec(command.view(), *spec);
    }
    return print_status;
  }

  let const do_make_spec = [&]() throws -> completion_spec {
    let spec = completion_spec{};
    spec.function_name = String{heap_allocator(), function_name};
    spec.word_list = String{heap_allocator(), word_list};
    spec.should_use_default = should_use_default;
    spec.defining_runtime = RuntimeState::capture(cxt);
    return spec;
  };

  if (is_default_completion) {
    LOG(Debug, "complete registering the default spec with function '%s'",
        function_name.c_str());
    cxt.register_default_completion_spec(do_make_spec());
    return 0;
  }

  for (let const &command : commands) {
    LOG(Debug, "complete registering spec for '%s'", command.c_str());
    cxt.register_completion_spec(command.view(), do_make_spec());
  }
  return 0;
}

} // namespace shit
