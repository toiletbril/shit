#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-neE] [arg ...]");

HELP_DESCRIPTION_DECL(
    "The echo builtin prints its arguments separated by spaces and ends with "
    "a newline. -n drops the trailing newline. -e interprets the backslash "
    "escapes and -E leaves them literal. The shit default interprets escapes "
    "the way dash does while still reading the three options, the bash mood "
    "leaves escapes literal unless -e asks, and the POSIX mood reads only a "
    "leading -n. This help shows only in the default mood with --help as the "
    "sole argument, since a POSIX or bash echo prints the literal text.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(ECHO_NO_NEWLINE, Bool, 'n', "", "Do not print the trailing newline.");
FLAG(ECHO_ESCAPES, Bool, 'e', "", "Interpret backslash escapes.");
FLAG(ECHO_NO_ESCAPES, Bool, 'E', "", "Leave backslash escapes literal.");

REGISTER_BUILTIN_FLAGS(Echo);

namespace shit {

Echo::Echo() = default;

pure fn Echo::kind() const wontthrow -> Builtin::Kind { return Kind::Echo; }

fn Echo::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  /* Only the shit default mood answers --help, and only as the sole
     argument, since bash and dash both print the literal text and a script
     may depend on that. */
  if (args.count() == 2 && args[1] == "--help" && !cxt.is_posix_mode() &&
      !cxt.is_bash_compatible())
  {
    SHOW_BUILTIN_HELP_AND_RETURN(ec);
  }

  LOG(All, "echo printing %zu arguments", args.count() - 1);

  usize start = 1;
  let should_suppress_newline = false;
  /* dash always interprets the backslash escapes and treats only a leading -n
     as an option, the POSIX behavior. bash leaves the escapes literal unless -e
     is given. The shit-native default interprets the escapes the way dash does,
     yet still reads -e, -E, and -n the way bash does, so a bash config that
     runs echo -e in the snapped session prints the way it expects rather than
     leaving a literal -e. The options combine, such as -ne. */
  let should_interpret_escapes = !cxt.is_bash_compatible();

  if (!cxt.is_posix_mode()) {
    while (start < args.count()) {
      const StringView arg = args[start].view();
      if (arg.length < 2 || arg[0] != '-') break;
      bool is_all_option_letters = true;
      for (usize k = 1; k < arg.length; k++)
        if (arg[k] != 'n' && arg[k] != 'e' && arg[k] != 'E') {
          is_all_option_letters = false;
          break;
        }
      if (!is_all_option_letters) break;
      for (usize k = 1; k < arg.length; k++) {
        if (arg[k] == 'n')
          should_suppress_newline = true;
        else if (arg[k] == 'e')
          should_interpret_escapes = true;
        else if (arg[k] == 'E')
          should_interpret_escapes = false;
      }
      start++;
    }
  } else {
    while (start < args.count() && args[start] == "-n") {
      should_suppress_newline = true;
      start++;
    }
  }

  let output = String{};
  let should_stop = false;

  for (usize i = start; i < args.count() && !should_stop; i++) {
    if (i > start) output += ' ';

    let const &arg = args[i];
    for (usize j = 0; j < arg.length(); j++) {
      if (!should_interpret_escapes || arg[j] != '\\' || j + 1 >= arg.length())
      {
        output += arg[j];
        continue;
      }

      let const escaped = arg[j + 1];
      j++;
      switch (escaped) {
      case 'a': output += '\a'; break;
      case 'b': output += '\b'; break;
      case 'f': output += '\f'; break;
      case 'n': output += '\n'; break;
      case 'r': output += '\r'; break;
      case 't': output += '\t'; break;
      case 'v': output += '\v'; break;
      /* \e and \E are the escape character, a bash extension the shit default
         reads too. dash does not, so POSIX mode leaves them literal. */
      case 'e':
      case 'E':
        if (cxt.is_posix_mode()) {
          output += '\\';
          output += escaped;
        } else {
          output += '\x1b';
        }
        break;
      case '\\': output += '\\'; break;
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
        output += static_cast<char>(value);
      } break;
      /* The bare \NNN form is one to three octal digits with no leading zero,
         so \101 is the byte A the way dash's XSI echo reads it. bash reads
         octal only in the \0NNN form, so in bash mode a bare \NNN stays
         literal. The first digit is already in hand, so up to two more follow.
       */
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7': {
        if (cxt.is_bash_compatible()) {
          output += '\\';
          output += escaped;
          break;
        }
        i32 value = escaped - '0';
        usize digit_count = 1;
        while (digit_count < 3 && j + 1 < arg.length() && arg[j + 1] >= '0' &&
               arg[j + 1] <= '7')
        {
          value = value * 8 + (arg[j + 1] - '0');
          j++;
          digit_count++;
        }
        output += static_cast<char>(value);
      } break;
      default:
        output += '\\';
        output += escaped;
        break;
      }

      if (should_stop) break;
    }
  }

  if (!should_suppress_newline && !should_stop) {
    output += '\n';
  }

  ec.print_to_stdout(output);

  return 0;
}

} /* namespace shit */
