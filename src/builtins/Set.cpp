#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* set toggles the shell options and rebinds the positional parameters. An
   option is named by a single letter after a minus to enable it or a plus to
   disable it, or by its long name after -o or +o. The letters and names that
   have no backing behavior yet are accepted without effect. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abCefhmnuvx] [+abCefhmnuvx] [-o name] [+o name] "
                   "[--] [arg ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace {

/* One shell option, named by its letter and its long name. set and get are the
   accessors on EvalContext, both null for an option accepted without effect. */
class SetOption
{
public:
  char letter;
  StringView name;
  void (EvalContext::*set)(bool);
  bool (EvalContext::*get)() const;
};

const SetOption SET_OPTIONS[] = {
    {'e',  "errexit",   &EvalContext::set_error_exit,    &EvalContext::error_exit },
    {'x',  "xtrace",    &EvalContext::set_echo_expanded,
     &EvalContext::should_echo_expanded                                           },
    {'u',  "nounset",   &EvalContext::set_error_unset,   &EvalContext::error_unset},
    {'a',  "allexport", &EvalContext::set_export_all,    &EvalContext::export_all },
    {'C',  "noclobber", &EvalContext::set_no_clobber,    &EvalContext::no_clobber },
    {'f',  "noglob",    &EvalContext::set_no_glob,       &EvalContext::no_glob    },
    {'n',  "noexec",    &EvalContext::set_no_exec,       &EvalContext::no_exec    },
    {'m',  "monitor",   &EvalContext::set_monitor,       &EvalContext::monitor    },
    /* failglob has no short letter, so '\0' keeps find_option_by_letter from
       ever matching a parsed option character. */
    {'\0', "failglob",  &EvalContext::set_failglob,      &EvalContext::failglob   },
    {'b',  "notify",    nullptr,                         nullptr                  },
    {'h',  "hashall",   nullptr,                         nullptr                  },
    {'v',  "verbose",   nullptr,                         nullptr                  },
};

const SetOption *find_option_by_letter(char letter) throws
{
  for (const SetOption &option : SET_OPTIONS)
    if (option.letter == letter) return &option;
  return nullptr;
}

const SetOption *find_option_by_name(StringView name) throws
{
  for (const SetOption &option : SET_OPTIONS)
    if (option.name == name) return &option;
  return nullptr;
}

bool option_is_on(const EvalContext &cxt, const SetOption &option) throws
{
  return option.get != nullptr ? (cxt.*(option.get))() : false;
}

/* The reusable command form that set -o and set +o print, one line each. */
String list_options(const EvalContext &cxt) throws
{
  String out{};
  for (const SetOption &option : SET_OPTIONS) {
    out += option_is_on(cxt, option) ? "set -o " : "set +o ";
    out += option.name;
    out += '\n';
  }
  return out;
}

} /* namespace */

Set::Set() = default;

pure Builtin::Kind Set::kind() const wontthrow { return Kind::Set; }

i32 Set::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* set with no arguments lists the shell variables. */
  if (args.count() == 1) {
    String out{};
    for (let const &assignment : cxt.sorted_variable_assignments()) {
      out += assignment.view();
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  ArrayList<String> operands{heap_allocator()};
  bool collecting_operands = false;
  bool should_rebind = false;

  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];

    if (collecting_operands) {
      operands.push(String{heap_allocator(), arg});
      continue;
    }

    if (arg == "--") {
      collecting_operands = true;
      should_rebind = true;
      continue;
    }

    /* The long form -o name and +o name names one option, or lists them all
       when no name follows. */
    if (arg == "-o" || arg == "+o") {
      let const enable = arg[0] == '-';
      if (i + 1 >= args.count()) {
        ec.print_to_stdout(list_options(cxt));
        continue;
      }
      ASSERT(i + 1 < args.count());
      let const &name = args[++i];
      let const option = find_option_by_name(name);
      if (option == nullptr)
        throw Error{"'" + name + "' is not a valid option name"};
      if (option->set != nullptr) (cxt.*(option->set))(enable);
      continue;
    }

    /* A minus enables each following letter, a plus disables each one. */
    if (arg.length() > 1 && (arg[0] == '-' || arg[0] == '+')) {
      let const enable = arg[0] == '-';
      for (usize c = 1; c < arg.length(); c++) {
        let const letter = arg[c];
        let const option = find_option_by_letter(letter);
        if (option == nullptr) {
          String invalid_option{};
          invalid_option += arg[0];
          invalid_option += letter;
          throw Error{"'" + invalid_option + "' is not a valid option"};
        }
        if (option->set != nullptr) (cxt.*(option->set))(enable);
      }
      continue;
    }

    /* The first non-option argument and everything after it rebinds the
       positional parameters. */
    collecting_operands = true;
    should_rebind = true;
    operands.push(String{heap_allocator(), arg});
  }

  if (should_rebind) cxt.set_positional_params(steal(operands));

  return 0;
}

} /* namespace shit */
