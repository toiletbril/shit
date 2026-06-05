#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

#include <string>

/* getopts optstring name [arg ...]. It parses one option per call from the
   positional parameters, or from explicit args, tracking progress through
   OPTIND and the per-argument index the EvalContext keeps. A leading colon in
   optstring selects the silent error mode. */

namespace shit {

Getopts::Getopts() = default;

Builtin::Kind Getopts::kind() const { return Kind::Getopts; }

i32 Getopts::execute(ExecContext &ec, EvalContext &cxt) const
{
  const ArrayList<String> &args = ec.args();
  if (args.size() < 3)
    throw Error{"getopts: usage: getopts optstring name [arg ...]"};

  const String &optstring = args[1];
  const String &name = args[2];
  bool is_silent = !optstring.empty() && optstring[0] == ':';

  ArrayList<String> operands{};
  if (args.size() > 3) {
    for (usize i = 3; i < args.size(); i++)
      operands.push(args[i]);
  } else {
    operands = cxt.positional_params();
  }

  i64 optind = 1;
  if (Maybe<String> value = cxt.get_variable_value("OPTIND"); value.has_value())
  {
    ErrorOr<i64> parsed = utils::parse_decimal_integer(*value);
    optind = parsed.is_error() ? 1 : parsed.value();
  }

  /* A script that resets OPTIND starts a fresh scan, so the per-argument index
     returns to the first letter. */
  if (optind != cxt.getopts_last_optind()) cxt.set_getopts_char_index(1);
  usize char_index = cxt.getopts_char_index();

  auto finish = [&](i32 code) -> i32 {
    cxt.set_shell_variable("OPTIND", std::to_string(optind));
    cxt.set_getopts_char_index(char_index);
    cxt.set_getopts_last_optind(optind);
    return code;
  };

  if (optind < 1 || static_cast<usize>(optind) > operands.size()) {
    cxt.set_shell_variable(name, "?");
    return finish(1);
  }

  const String &current = operands[static_cast<usize>(optind) - 1];
  if (current.length() < 2 || current[0] != '-') {
    cxt.set_shell_variable(name, "?");
    return finish(1);
  }
  if (current == "--") {
    optind++;
    cxt.set_shell_variable(name, "?");
    return finish(1);
  }

  char option = current[char_index];
  String option_as_string{};
  option_as_string.push(option);
  Maybe<usize> spec = optstring.find_character(option);

  auto advance_letter = [&]() {
    char_index++;
    if (char_index >= current.length()) {
      optind++;
      char_index = 1;
    }
  };

  if (option == ':' || !spec.has_value()) {
    advance_letter();
    cxt.set_shell_variable(name, "?");
    if (is_silent) {
      cxt.set_shell_variable("OPTARG", option_as_string);
    } else {
      cxt.unset_shell_variable("OPTARG");
      shit::print_error(StringView{"getopts: illegal option -- "} +
                                    option_as_string + "\n");
    }
    return finish(0);
  }

  bool wants_argument =
      *spec + 1 < optstring.length() && optstring[*spec + 1] == ':';
  if (wants_argument) {
    if (char_index + 1 < current.length()) {
      StringView optarg = current.substring(char_index + 1);
      cxt.set_shell_variable("OPTARG", optarg);
      optind++;
      char_index = 1;
    } else if (static_cast<usize>(optind) < operands.size()) {
      const String &optarg = operands[static_cast<usize>(optind)];
      cxt.set_shell_variable("OPTARG", optarg);
      optind += 2;
      char_index = 1;
    } else {
      optind++;
      char_index = 1;
      if (is_silent) {
        cxt.set_shell_variable(name, ":");
        cxt.set_shell_variable("OPTARG", option_as_string);
      } else {
        cxt.set_shell_variable(name, "?");
        cxt.unset_shell_variable("OPTARG");
        shit::print_error(
            StringView{"getopts: option requires an argument -- "} +
            option_as_string + "\n");
      }
      return finish(0);
    }
    cxt.set_shell_variable(name, option_as_string);
    return finish(0);
  }

  /* An option that takes no argument leaves OPTARG unset. */
  advance_letter();
  cxt.unset_shell_variable("OPTARG");
  cxt.set_shell_variable(name, option_as_string);
  return finish(0);
}

} /* namespace shit */
