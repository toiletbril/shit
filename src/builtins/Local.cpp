#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* local declares a variable local to the current function, so the value it had
   in the caller returns when the function ends. It is an error outside a
   function. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("name[=value] ...");
HELP_DESCRIPTION_DECL(
    "The local builtin declares each named variable local to the current "
    "function, so the value it had in the caller returns when the function "
    "ends, and it assigns a value when name=value is given. It is an error "
    "outside a function.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Local::Local() = default;

pure Builtin::Kind Local::kind() const wontthrow { return Kind::Local; }

i32 Local::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  /* The attribute flags are read straight off the arguments rather than through
     the shared flag parser, since that parser rejects an unknown letter and the
     bash attribute letters are not declared as flags here. */
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (!cxt.in_function_scope())
    throw Error{"'local' can only be used inside a function"};

  /* Leading flags carry the bash attributes. -a declares an indexed array and
     -A an associative one, the rest are accepted without backing behavior so a
     script that sets them keeps running. */
  bool make_indexed = false;
  bool make_associative = false;
  usize first_name = 1;
  for (; first_name < args.count(); first_name++) {
    const StringView arg = args[first_name].view();
    if (arg.length < 1 || arg[0] != '-') break;
    if (arg == "--") {
      first_name++;
      break;
    }
    for (usize c = 1; c < arg.length; c++) {
      switch (arg[c]) {
      case 'a': make_indexed = true; break;
      case 'A': make_associative = true; break;
      case 'i':
      case 'r':
      case 'x':
      case 'l':
      case 'u':
      case 'n': break;
      default: {
        String invalid{};
        invalid += arg[c];
        throw Error{"local: -" + invalid + ": invalid option"};
      }
      }
    }
  }

  for (usize i = first_name; i < args.count(); i++) {
    let const &arg = args[i];
    let const equals_position = arg.find_character('=');

    /* Record the shadowed binding before overwriting it, so leaving the
       function restores it. A bare name declares the local without touching the
       value, so the currently-visible binding from the caller stays readable
       until the body assigns the name, matching dash. */
    let const name = equals_position.has_value()
                         ? arg.substring_of_length(0, *equals_position)
                         : arg.view();
    cxt.declare_local(name);

    if (make_associative) {
      cxt.declare_associative_array(name);
    } else if (make_indexed) {
      if (cxt.lookup_indexed_array(name) == nullptr)
        cxt.set_indexed_array(name, ArrayList<String>{heap_allocator()});
    } else if (equals_position.has_value()) {
      cxt.set_shell_variable(name, arg.substring(*equals_position + 1));
    }
  }

  return 0;
}

} /* namespace shit */
