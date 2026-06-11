#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* complete registers a programmable-completion spec for a command, the bash
   builtin a completion script calls, such as complete -o default -F _name name.
   The spec is stored on the context, and the interactive completion engine
   consults it when an argument to that command is completed. The -F function
   and the -W word list drive the candidates, and -o default falls back to
   filename completion when the spec yields nothing. The remaining options are
   accepted so a config sources cleanly. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abcdefgjksuv] [-o option] [-A action] [-G globpat] "
                   "[-W wordlist] [-F function] [-C command] [-X filterpat] "
                   "[-P prefix] [-S suffix] [-pr] [name ...]");
HELP_DESCRIPTION_DECL(
    "The complete builtin registers a programmable-completion spec for each "
    "named command, the way a bash completion script does. The interactive "
    "engine consults the spec when an argument of that command completes. "
    "The -W word list filters on every keystroke, the -F function runs on an "
    "explicit tab, -o default falls back to filename completion when the "
    "spec yields nothing, and a -D default spec drives the bash-completion "
    "dynamic loader with its 124 retry protocol.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

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

  /* The spec the options build, then registered for each named command. */
  let function_name = String{};
  let word_list = String{};
  bool use_default = false;
  bool is_default_completion = false;
  bool should_print_specs = false;
  let commands = ArrayList<String>{};

  for (usize i = 1; i < args.count();) {
    let const arg = args[i].view();
    /* A double dash ends option parsing, so complete -p -- man reads man as a
       command name rather than another option. */
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
    /* -D registers the default completion, used for a command with no spec of
       its own, the way bash-completion attaches its dynamic loader. */
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
        use_default = true;
      i++;
      continue;
    }
    /* These options carry a value that is accepted without effect, so the value
       argument is skipped with them. */
    if (arg == "-A" || arg == "-G" || arg == "-C" || arg == "-X" ||
        arg == "-P" || arg == "-S")
    {
      i += 2;
      continue;
    }
    /* Any other dash option, such as -p, -r, -f, or the action letters, is
       accepted without effect. */
    if (!arg.is_empty() && arg[0] == '-') {
      i++;
      continue;
    }
    commands.push_managed(arg);
    i++;
  }

  /* -p prints the named specs, or every spec with no names, in a form the
     shell can replay, and reports failure when a named command has no spec.
     The bash-completion loader probes exactly that, a successful print means
     sourcing the completion file registered something. */
  if (should_print_specs) {
    let const print_one_spec = [&](StringView command,
                                   const completion_spec &spec) throws {
      let line = String{"complete "};
      if (spec.use_default) line += "-o default ";
      if (!spec.function_name.is_empty()) {
        line += "-F ";
        line += spec.function_name.view();
        line += ' ';
      }
      if (!spec.word_list.is_empty()) {
        line += "-W '";
        line += spec.word_list.view();
        line += "' ";
      }
      line += command;
      line += '\n';
      ec.print_to_stdout(line.view());
    };
    if (commands.is_empty()) {
      cxt.completion_specs().for_each(
          [&](StringView command, const completion_spec &spec) {
            print_one_spec(command, spec);
          });
      return 0;
    }
    i32 print_status = 0;
    for (const String &command : commands) {
      const completion_spec *spec = cxt.lookup_completion_spec(command.view());
      if (spec == nullptr) {
        print_status = 1;
        continue;
      }
      print_one_spec(command.view(), *spec);
    }
    return print_status;
  }

  if (is_default_completion) {
    LOG(verbosity::Debug,
        "complete registering the default spec with function '%s'",
        function_name.c_str());
    let spec = completion_spec{};
    spec.function_name = String{heap_allocator(), function_name};
    spec.word_list = String{heap_allocator(), word_list};
    spec.use_default = use_default;
    cxt.register_default_completion_spec(steal(spec));
    return 0;
  }

  for (const String &command : commands) {
    LOG(verbosity::Debug, "complete registering spec for '%s'",
        command.c_str());
    let spec = completion_spec{};
    spec.function_name = String{heap_allocator(), function_name};
    spec.word_list = String{heap_allocator(), word_list};
    spec.use_default = use_default;
    cxt.register_completion_spec(command.view(), steal(spec));
  }
  return 0;
}

} /* namespace shit */
