#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

#include <string>

/* Reads one line from standard input and splits it on IFS into the named
   variables, the last variable taking the remainder. The flag parser rejects
   an unknown option. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("read [-r] [name ...]");

FLAG(READ_RAW, Bool, 'r', "", "Do not treat a backslash as an escape.");

namespace shit {

Read::Read() = default;

Builtin::Kind
Read::kind() const
{
  return Kind::Read;
}

i32
Read::execute(ExecContext &ec, EvalContext &cxt) const
{
  /* -r is accepted, and since backslash processing is not done here the read is
     raw either way. The String arguments are copied into std::string for the
     flag parser, which works over std::string. */
  std::vector<std::string> arguments{};
  for (usize i = 1; i < ec.args().size(); i++) {
    const String &argument = ec.args()[i];
    arguments.emplace_back(argument.c_str(), argument.size());
  }
  std::vector<std::string> names = parse_flags_vec(FLAG_LIST, arguments);
  SHIT_DEFER { reset_flags(FLAG_LIST); };

  if (names.empty()) names.emplace_back("REPLY");

  Maybe<std::string> read_line = utils::read_line_from_standard_input();
  if (!read_line) {
    for (const std::string &name : names)
      cxt.set_shell_variable(name, "");
    return 1;
  }
  const std::string &line = *read_line;

  std::string field_separators =
      cxt.get_variable_value("IFS").value_or(" \t\n");
  auto is_separator = [&field_separators](char c) {
    return field_separators.find(c) != std::string::npos;
  };

  usize cursor = 0;
  for (usize i = 0; i < names.size(); i++) {
    while (cursor < line.length() && is_separator(line[cursor]))
      cursor++;

    if (i + 1 == names.size()) {
      /* The last variable receives the rest of the line with trailing
         separators trimmed. */
      std::string rest = line.substr(cursor);
      while (!rest.empty() && is_separator(rest.back()))
        rest.pop_back();
      cxt.set_shell_variable(names[i], rest);
    } else {
      usize start = cursor;
      while (cursor < line.length() && !is_separator(line[cursor]))
        cursor++;
      cxt.set_shell_variable(names[i], line.substr(start, cursor - start));
    }
  }

  return 0;
}

} /* namespace shit */
