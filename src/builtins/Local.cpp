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
    throw Error{"Unable to declare a local variable outside a function"};

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
        let invalid = String{};
        invalid += arg[c];
        throw Error{"Unable to declare a local because '-" + invalid +
                    "' is not a valid option"};
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
    StringView name = equals_position.has_value()
                          ? arg.substring_of_length(0, *equals_position)
                          : arg.view();
    /* process_args passes a local append through as name+=value, so a trailing
       plus on the name marks the append and is stripped before the binding. */
    let const is_append = !name.is_empty() && name[name.count() - 1] == '+';
    if (is_append) name = name.substring_of_length(0, name.count() - 1);

    /* The append reads the name's own value only when it is already local in
       this scope, so a first local += starts from empty the way bash localizes
       it fresh, not from the outer value the new local shadows. */
    let const was_already_local =
        is_append && cxt.is_local_in_current_scope(name);
    cxt.declare_local(name);

    if (make_associative) {
      cxt.declare_associative_array(name);
    } else if (make_indexed) {
      if (cxt.lookup_indexed_array(name) == nullptr)
        cxt.set_indexed_array(name, ArrayList<String>{heap_allocator()});
    } else if (equals_position.has_value()) {
      let const value = arg.substring(*equals_position + 1);
      if (is_append) {
        /* The appended value is transient, copied into the variable store by
           set_shell_variable, so it lives on the per-command scratch arena. */
        let appended = String{cxt.scratch_allocator()};
        if (was_already_local)
          if (let const existing = cxt.get_variable_value(name))
            appended.append(existing->view());
        appended.append(value);
        cxt.set_shell_variable(name, appended.view());
      } else {
        cxt.set_shell_variable(name, value);
      }
    }
  }

  return 0;
}

} /* namespace shit */
