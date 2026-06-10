#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* getopts optstring name [arg ...]. It parses one option per call from the
   positional parameters, or from explicit args, tracking progress through
   OPTIND and the per-argument index the EvalContext keeps. A leading colon in
   optstring selects the silent error mode. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("optstring name [argument ...]");

HELP_DESCRIPTION_DECL(
    "The getopts builtin parses the positional parameters, or the explicit "
    "arguments, against optstring one option at a time, storing the option "
    "letter in name and tracking progress through OPTIND. A leading colon in "
    "optstring selects the silent error mode where a bad option is reported "
    "through name and OPTARG rather than a message.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Getopts::Getopts() = default;

pure fn Getopts::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Getopts;
}

fn Getopts::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() < 3)
    throw Error{"Unable to parse options because an optstring and a name "
                "argument are required"};

  let const &optstring = args[1];
  let const &name = args[2];
  let const is_silent = !optstring.is_empty() && optstring[0] == ':';

  let operands = ArrayList<String>{};
  if (args.count() > 3) {
    for (usize i = 3; i < args.count(); i++)
      operands.push(args[i]);
  } else {
    operands = cxt.positional_params();
  }

  i64 optind = 1;
  if (Maybe<String> value = cxt.get_variable_value("OPTIND"); value.has_value())
  {
    let const parsed = utils::parse_decimal_integer(*value);
    optind = parsed.is_error() ? 1 : parsed.value();
  }

  /* A script that resets OPTIND starts a fresh scan, so the per-argument index
     returns to the first letter. */
  if (optind != cxt.getopts_last_optind()) cxt.set_getopts_char_index(1);
  let char_index = cxt.getopts_char_index();

  auto finish = [&](i32 code) -> i32 {
    cxt.set_shell_variable("OPTIND", utils::int_to_text(optind));
    cxt.set_getopts_char_index(char_index);
    cxt.set_getopts_last_optind(optind);
    return code;
  };

  if (optind < 1 || static_cast<usize>(optind) > operands.count()) {
    cxt.set_shell_variable(name, "?");
    return finish(1);
  }

  ASSERT(static_cast<usize>(optind) - 1 < operands.count());
  let const &current = operands[static_cast<usize>(optind) - 1];
  if (current.length() < 2 || current[0] != '-') {
    cxt.set_shell_variable(name, "?");
    return finish(1);
  }
  if (current == "--") {
    optind++;
    cxt.set_shell_variable(name, "?");
    return finish(1);
  }

  /* The per-argument index is persisted across calls, so a stale index can run
     past a current operand that the caller rebound to a shorter word while
     OPTIND stayed put. An index at or past the operand length is treated as a
     fresh operand, returning to the first letter rather than reading past the
     end. */
  if (char_index >= current.length()) char_index = 1;
  ASSERT(char_index < current.length());
  let const option = current[char_index];
  let option_as_string = String{};
  option_as_string.push(option);
  let const spec = optstring.find_character(option);

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

  let const wants_argument =
      *spec + 1 < optstring.length() && optstring[*spec + 1] == ':';
  if (wants_argument) {
    if (char_index + 1 < current.length()) {
      let const optarg = current.substring(char_index + 1);
      cxt.set_shell_variable("OPTARG", optarg);
      optind++;
      char_index = 1;
    } else if (static_cast<usize>(optind) < operands.count()) {
      let const &optarg = operands[static_cast<usize>(optind)];
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
