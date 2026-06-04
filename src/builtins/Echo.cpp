#include "../Builtin.hpp"
#include "../Utils.hpp"

namespace shit {

Echo::Echo() = default;

Builtin::Kind
Echo::kind() const
{
  return Kind::Echo;
}

i32
Echo::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  const std::vector<std::string> &args = ec.args();

  /* Match dash, where only a leading -n is an option and everything after it,
     including -e, is literal text, and backslash escapes are always
     interpreted. */
  usize start = 1;
  bool should_suppress_newline = false;
  while (start < args.size() && args[start] == "-n") {
    should_suppress_newline = true;
    start++;
  }

  std::string buf{};
  bool should_stop = false;

  for (usize i = start; i < args.size() && !should_stop; i++) {
    if (i > start) buf += ' ';

    const std::string &arg = args[i];
    for (usize j = 0; j < arg.length(); j++) {
      if (arg[j] != '\\' || j + 1 >= arg.length()) {
        buf += arg[j];
        continue;
      }

      char escaped = arg[j + 1];
      j++;
      switch (escaped) {
      case 'a': buf += '\a'; break;
      case 'b': buf += '\b'; break;
      case 'f': buf += '\f'; break;
      case 'n': buf += '\n'; break;
      case 'r': buf += '\r'; break;
      case 't': buf += '\t'; break;
      case 'v': buf += '\v'; break;
      case '\\': buf += '\\'; break;
      /* \c stops all output and drops the trailing newline. */
      case 'c': should_stop = true; break;
      /* \0NNN is up to three octal digits. */
      case '0': {
        i32 value = 0;
        usize digit_count = 0;
        while (digit_count < 3 && j + 1 < arg.length() && arg[j + 1] >= '0' &&
               arg[j + 1] <= '7')
        {
          value = value * 8 + (arg[j + 1] - '0');
          j++;
          digit_count++;
        }
        buf += static_cast<char>(value);
      } break;
      default:
        buf += '\\';
        buf += escaped;
        break;
      }

      if (should_stop) break;
    }
  }

  if (!should_suppress_newline && !should_stop) buf += '\n';

  ec.print_to_stdout(buf);

  return 0;
}

} /* namespace shit */
