#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* The minus form of the options goes through the standard flag parser, which
   rejects an unknown option and understands -- and grouped letters. The plus
   form turns an option off and the flag parser does not model it, so it is
   handled directly. Only e, x, and u take effect, the other POSIX letters are
   accepted without effect. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("set [-eux] [+eux] [--] [arg ...]");

FLAG(SET_ERROR_EXIT, Bool, 'e', "", "Exit immediately when a command fails.");
FLAG(SET_XTRACE, Bool, 'x', "", "Print each command before it runs.");
FLAG(SET_NOUNSET, Bool, 'u', "", "Treat an unset variable as an error.");
FLAG(SET_ALLEXPORT, Bool, 'a', "", "Accepted with no effect.");
FLAG(SET_NOTIFY, Bool, 'b', "", "Accepted with no effect.");
FLAG(SET_NOCLOBBER, Bool, 'C', "", "Accepted with no effect.");
FLAG(SET_NOGLOB, Bool, 'f', "", "Accepted with no effect.");
FLAG(SET_HASHALL, Bool, 'h', "", "Accepted with no effect.");
FLAG(SET_MONITOR, Bool, 'm', "", "Accepted with no effect.");
FLAG(SET_NOEXEC, Bool, 'n', "", "Accepted with no effect.");
FLAG(SET_VERBOSE, Bool, 'v', "", "Accepted with no effect.");

namespace shit {

Set::Set() = default;

Builtin::Kind
Set::kind() const
{
  return Kind::Set;
}

i32
Set::execute(ExecContext &ec, EvalContext &cxt) const
{
  const std::vector<std::string> &args = ec.args();

  /* set with no arguments lists the shell variables. */
  if (args.size() == 1) {
    std::string out{};
    for (const std::string &assignment : cxt.sorted_variable_assignments())
      out += assignment + "\n";
    ec.print_to_stdout(out);
    return 0;
  }

  bool disable_error_exit = false;
  bool disable_xtrace = false;
  bool disable_nounset = false;
  bool saw_end_of_options = false;

  /* The parser treats its first entry as a value, not the builtin name, so the
     name is left out. */
  std::vector<std::string> for_parser{};
  for (usize i = 1; i < args.size(); i++) {
    const std::string &arg = args[i];
    if (arg == "--") saw_end_of_options = true;

    if (arg.length() > 1 && arg[0] == '+') {
      for (usize c = 1; c < arg.length(); c++) {
        char option = arg[c];
        bool is_known = false;
        for (const Flag *flag : FLAG_LIST)
          if (flag->short_name() == option) is_known = true;
        if (!is_known)
          throw Error{"set: +" + std::string{option} + ": invalid option"};

        if (option == 'e')
          disable_error_exit = true;
        else if (option == 'x')
          disable_xtrace = true;
        else if (option == 'u')
          disable_nounset = true;
      }
      continue;
    }

    for_parser.push_back(arg);
  }

  std::vector<std::string> operands = parse_flags_vec(FLAG_LIST, for_parser);
  SHIT_DEFER { reset_flags(FLAG_LIST); };

  if (FLAG_SET_ERROR_EXIT.is_enabled()) cxt.set_error_exit(true);
  if (FLAG_SET_XTRACE.is_enabled()) cxt.set_echo_expanded(true);
  if (FLAG_SET_NOUNSET.is_enabled()) cxt.set_error_unset(true);
  if (disable_error_exit) cxt.set_error_exit(false);
  if (disable_xtrace) cxt.set_echo_expanded(false);
  if (disable_nounset) cxt.set_error_unset(false);

  /* Rebind the positional parameters only when operands or -- were given, so a
     bare set -e leaves them alone. */
  if (!operands.empty() || saw_end_of_options)
    cxt.set_positional_params(std::move(operands));

  return 0;
}

} /* namespace shit */
