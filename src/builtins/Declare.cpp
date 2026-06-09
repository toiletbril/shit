#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"

/* declare and its alias typeset set variable attributes. The bash-specific use
   here is -A to make a name an associative array and -a to make it indexed, so
   a later subscript assignment routes to the right store. A plain name=value
   sets a scalar, and -x marks it for the environment. The other attribute
   letters are accepted without effect. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-aAirxp] [name[=value] ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Declare::Declare() = default;

pure Builtin::Kind Declare::kind() const wontthrow { return Kind::Declare; }

i32 Declare::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  bool make_associative = false;
  bool make_indexed = false;
  bool do_export = false;

  usize i = 1;
  for (; i < args.count(); i++) {
    const StringView arg = args[i].view();
    if (arg.length < 1 || (arg[0] != '-' && arg[0] != '+')) break;
    if (arg == "--") {
      i++;
      break;
    }
    for (usize c = 1; c < arg.length; c++) {
      switch (arg[c]) {
      case 'A': make_associative = true; break;
      case 'a': make_indexed = true; break;
      case 'x': do_export = true; break;
      /* The remaining attribute letters carry no backing behavior yet and are
         accepted so a script that sets them keeps running. */
      case 'i':
      case 'r':
      case 'g':
      case 'l':
      case 'u':
      case 'n':
      case 'f':
      case 't':
      case 'p': break;
      default: {
        String invalid{};
        invalid += arg[0];
        invalid += arg[c];
        throw Error{"declare: '" + invalid + "' is not a valid option"};
      }
      }
    }
  }

  for (; i < args.count(); i++) {
    const StringView operand = args[i].view();
    let const equals = operand.find_character('=');
    const StringView name =
        equals.has_value() ? operand.substring_of_length(0, *equals) : operand;
    const StringView value =
        equals.has_value() ? operand.substring(*equals + 1) : StringView{};

    if (make_associative) {
      cxt.declare_associative_array(name);
    } else if (make_indexed) {
      if (cxt.lookup_indexed_array(name) == nullptr)
        cxt.set_indexed_array(name, ArrayList<String>{heap_allocator()});
    } else if (equals.has_value()) {
      cxt.set_shell_variable(name, value);
      if (do_export) {
        cxt.record_environment_change(name);
        os::set_environment_variable(name, value);
      }
    }
  }

  return 0;
}

} /* namespace shit */
