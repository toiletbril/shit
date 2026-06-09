#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

namespace shit {

Echo::Echo() = default;

pure fn Echo::kind() const wontthrow -> Builtin::Kind { return Kind::Echo; }

fn Echo::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  usize start = 1;
  let should_suppress_newline = false;
  /* dash always interprets the backslash escapes and treats only a leading -n
     as an option. bash leaves the escapes literal unless -e is given, and reads
     -e, -E, and -n as leading options, alone or combined such as -ne. */
  let interpret_escapes = !cxt.is_bash_compatible();

  if (cxt.is_bash_compatible()) {
    while (start < args.count()) {
      const StringView arg = args[start].view();
      if (arg.length < 2 || arg[0] != '-') break;
      bool all_option_letters = true;
      for (usize k = 1; k < arg.length; k++)
        if (arg[k] != 'n' && arg[k] != 'e' && arg[k] != 'E') {
          all_option_letters = false;
          break;
        }
      if (!all_option_letters) break;
      for (usize k = 1; k < arg.length; k++) {
        if (arg[k] == 'n')
          should_suppress_newline = true;
        else if (arg[k] == 'e')
          interpret_escapes = true;
        else if (arg[k] == 'E')
          interpret_escapes = false;
      }
      start++;
    }
  } else {
    while (start < args.count() && args[start] == "-n") {
      should_suppress_newline = true;
      start++;
    }
  }

  let buf = String{};
  let should_stop = false;

  for (usize i = start; i < args.count() && !should_stop; i++) {
    if (i > start) buf += ' ';

    let const &arg = args[i];
    for (usize j = 0; j < arg.length(); j++) {
      if (!interpret_escapes || arg[j] != '\\' || j + 1 >= arg.length()) {
        buf += arg[j];
        continue;
      }

      let const escaped = arg[j + 1];
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
      /* \0NNN reads up to three octal digits after the leading zero. */
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
      /* The bare \NNN form is one to three octal digits with no leading zero,
         so \101 is the byte A the way dash's XSI echo reads it. The first digit
         is already in hand, so up to two more follow. */
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7': {
        i32 value = escaped - '0';
        usize digit_count = 1;
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
