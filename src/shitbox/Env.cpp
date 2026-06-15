#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[NAME=value ...] [command [argument ...]]");

HELP_DESCRIPTION_DECL(
    "The env utility prints the environment when given no command. With "
    "leading "
    "NAME=value assignments and a command it runs the command with those "
    "variables added, then restores the previous values.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

/* Whether the operand is a NAME=value assignment, a valid identifier left of an
   '=' sign. A leading word without one ends the assignment run and starts the
   command. */
static fn is_assignment(StringView text) wontthrow -> bool
{
  let const equals = text.find_character('=');
  if (!equals.has_value() || *equals == 0) return false;
  for (usize i = 0; i < *equals; i++) {
    let const c = text[i];
    let const is_name = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
    if (!is_name) return false;
    if (i == 0 && c >= '0' && c <= '9') return false;
  }
  return true;
}

static fn print_environment(const ExecContext &ec) throws -> void
{
  let output = String{};
  for (const String &name : os::environment_names()) {
    let const value = os::get_environment_variable(name.view());
    output += name.view();
    output += '=';
    if (value.has_value()) output += value->view();
    output += '\n';
  }
  ec.print_to_stdout(output);
}

fn util_env(const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) {
    print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                    FLAG_LIST);
    return 0;
  }

  /* The leading NAME=value words set the environment for the command. The
     previous value of each name is saved so it can be put back once the command
     finishes, since env changes the environment for that command alone. */
  ArrayList<String> saved_names{};
  ArrayList<String> saved_values{};
  ArrayList<bool> was_present{};
  usize first_command = 0;
  while (first_command < operands.count() &&
         is_assignment(operands[first_command].view()))
  {
    let const text = operands[first_command].view();
    let const equals = *text.find_character('=');
    let const name = text.substring_of_length(0, equals);
    let const value = text.substring(equals + 1);

    let const previous = os::get_environment_variable(name);
    saved_names.push(String{name});
    saved_values.push(previous.has_value() ? previous->clone() : String{});
    was_present.push(previous.has_value());

    os::set_environment_variable(name, value);
    first_command++;
  }

  defer
  {
    for (usize i = 0; i < saved_names.count(); i++) {
      if (was_present[i])
        os::set_environment_variable(saved_names[i].view(),
                                     saved_values[i].view());
      else
        os::unset_environment_variable(saved_names[i].view());
    }
  };

  if (first_command >= operands.count()) {
    print_environment(ec);
    return 0;
  }

  /* The command runs in the shell with the assignments already applied, so the
     remaining operands are single-quoted into one line and evaluated. The
     quotes keep each operand a literal word, with an embedded quote escaped the
     way a shell expects, so a value with a space or a glob is not re-expanded.
   */
  let command_line = String{};
  for (usize i = first_command; i < operands.count(); i++) {
    if (i > first_command) command_line += ' ';
    command_line += '\'';
    let const word = operands[i].view();
    for (usize k = 0; k < word.length; k++) {
      if (word[k] == '\'')
        command_line += "'\\''";
      else
        command_line.push(word[k]);
    }
    command_line += '\'';
  }

  return cxt.run_source(command_line.view(), "env", true, ec.source_location(),
                        StringView{"env"});
}

} /* namespace shitbox */

} /* namespace shit */
