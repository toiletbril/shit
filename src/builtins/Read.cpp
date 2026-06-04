#include "../Builtin.hpp"
#include "../Eval.hpp"

#include <iostream>
#include <string>

/* Reads one line from standard input and splits it on IFS into the named
   variables, the last variable taking the remainder. */

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
  std::vector<std::string> names{ec.args().begin() + 1, ec.args().end()};

  /* -r is accepted, and since backslash processing is not done here the read is
     raw either way. */
  if (!names.empty() && names.front() == "-r") names.erase(names.begin());
  if (names.empty()) names.emplace_back("REPLY");

  std::string line{};
  if (!std::getline(std::cin, line)) {
    for (const std::string &name : names)
      cxt.set_shell_variable(name, "");
    return 1;
  }

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
