#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

/* declare and its alias typeset set variable attributes. The bash-specific use
   here is -A to make a name an associative array and -a to make it indexed, so
   a later subscript assignment routes to the right store. A plain name=value
   sets a scalar, -x marks it for the environment, and -i marks it as an
   integer so every assignment to it evaluates as arithmetic. The other
   attribute letters are accepted without effect. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-aAirxp] [name[=value] ...]");

HELP_DESCRIPTION_DECL(
    "The declare builtin, also spelled typeset, declares variables and sets "
    "their attributes. -a declares an indexed array, -A an associative array, "
    "-i an integer whose assignments evaluate as arithmetic, -r a readonly, "
    "-x an exported variable, and -p prints the declarations. +i removes the "
    "integer attribute. In bash mode it backs the array and associative "
    "types.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace {

/* The value a declare -p line wraps in double quotes, with the characters that
   are special inside double quotes escaped, so the printed line reloads to the
   same value the way bash quotes it. */
String quote_for_declare(StringView value) throws
{
  let quoted = String{};
  for (usize i = 0; i < value.length; i++) {
    const char c = value[i];
    if (c == '"' || c == '\\' || c == '$' || c == '`') quoted += '\\';
    quoted += c;
  }
  return quoted;
}

} /* namespace */

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
  bool do_print = false;
  bool mark_integer_attribute = false;
  bool unmark_integer_attribute = false;

  usize i = 1;
  for (; i < args.count(); i++) {
    const StringView arg = args[i].view();
    if (arg.length < 1 || (arg[0] != '-' && arg[0] != '+')) break;
    if (arg == "--") {
      i++;
      break;
    }
    /* A leading plus removes an attribute the way bash spells declare +i, so
       the sign decides whether a letter marks or unmarks. */
    let const is_remove_form = arg[0] == '+';
    for (usize c = 1; c < arg.length; c++) {
      switch (arg[c]) {
      case 'A': make_associative = true; break;
      case 'a': make_indexed = true; break;
      case 'x': do_export = true; break;
      case 'p': do_print = true; break;
      case 'i':
        if (is_remove_form)
          unmark_integer_attribute = true;
        else
          mark_integer_attribute = true;
        break;
      /* The remaining attribute letters carry no backing behavior yet and are
         accepted so a script that sets them keeps running. */
      case 'r':
      case 'g':
      case 'l':
      case 'u':
      case 'n':
      case 'f':
      case 't': break;
      default: {
        let invalid = String{};
        invalid += arg[0];
        invalid += arg[c];
        throw Error{"Unable to run declare because '" + invalid +
                    "' is not a valid option"};
      }
      }
    }
  }

  /* declare -p NAME prints the current declaration of each name in the bash
     syntax, the attribute flag then the name and a quoted value or an array
     literal, so a script can reload the state. An unknown name is an error. */
  if (do_print) {
    i32 status = 0;
    for (; i < args.count(); i++) {
      const StringView name = args[i].view();
      if (const ArrayList<String> *elements = cxt.lookup_indexed_array(name)) {
        let line = String{"declare -a "};
        line.append(name);
        line += "=(";
        for (usize e = 0; e < elements->count(); e++) {
          if (e > 0) line += ' ';
          line += '[';
          line += utils::int_to_text(static_cast<i64>(e));
          line += "]=\"";
          line += quote_for_declare((*elements)[e].view());
          line += '"';
        }
        line += ")\n";
        ec.print_to_stdout(line.view());
      } else if (cxt.is_associative_array(name)) {
        const ArrayList<String> keys = cxt.associative_keys(name);
        const ArrayList<String> values = cxt.associative_values(name);
        let line = String{"declare -A "};
        line.append(name);
        line += "=(";
        for (usize e = 0; e < keys.count(); e++) {
          line += '[';
          line.append(keys[e].view());
          line += "]=\"";
          if (e < values.count()) line += quote_for_declare(values[e].view());
          line += "\" ";
        }
        line += ")\n";
        ec.print_to_stdout(line.view());
      } else if (const Maybe<String> value = cxt.get_variable_value(name)) {
        /* The attribute letters compose the way bash prints declare -ix, and a
           scalar with no attribute prints the bare double dash. */
        let attribute = String{"-"};
        if (cxt.is_integer_variable(name)) attribute += 'i';
        if (os::get_environment_variable(name).has_value()) attribute += 'x';
        if (attribute.count() == 1) attribute += '-';
        let line = String{"declare "};
        line.append(attribute.view());
        line += ' ';
        line.append(name);
        line += "=\"";
        line += quote_for_declare(value->view());
        line += "\"\n";
        ec.print_to_stdout(line.view());
      } else {
        report_soft_builtin_error(ec, cxt, StringView{"'"} + name +
                                               "' is not defined");
        status = 1;
      }
    }
    return status;
  }

  for (; i < args.count(); i++) {
    const StringView operand = args[i].view();
    let const equals = operand.find_character('=');
    StringView name =
        equals.has_value() ? operand.substring_of_length(0, *equals) : operand;
    const StringView value =
        equals.has_value() ? operand.substring(*equals + 1) : StringView{};

    /* process_args passes a declare append through as name+=value, so a
       trailing plus on the name marks the append and is stripped before the
       attributes apply. */
    let const is_append = equals.has_value() && !name.is_empty() &&
                          name[name.count() - 1] == '+';
    if (is_append) name = name.substring_of_length(0, name.count() - 1);

    /* The attribute applies before the assignment, so declare -i x+=3 already
       adds on this command the way bash applies the integer mark first. */
    if (mark_integer_attribute) cxt.mark_integer(name);
    if (unmark_integer_attribute) cxt.unmark_integer(name);

    if (make_associative) {
      cxt.declare_associative_array(name);
    } else if (make_indexed) {
      if (cxt.lookup_indexed_array(name) == nullptr)
        cxt.set_indexed_array(name, ArrayList<String>{heap_allocator()});
    } else if (equals.has_value()) {
      if (is_append) {
        /* The appended value is transient, copied into the variable store by
           set_shell_variable, so it lives on the per-command scratch arena. An
           integer name joins the appended expression for the arithmetic in
           the store rather than concatenating it. */
        let appended = String{cxt.scratch_allocator()};
        if (let const existing = cxt.get_variable_value(name))
          appended.append(existing->view());
        if (cxt.is_integer_variable(name))
          cxt.append_integer_expression(appended, value);
        else
          appended.append(value);
        cxt.set_shell_variable(name, appended.view());
      } else {
        cxt.set_shell_variable(name, value);
      }
      if (do_export) {
        cxt.record_environment_change(name);
        /* The store may have rewritten the value, an integer name stores the
           arithmetic result, so the environment receives the stored value
           rather than the raw text. */
        let const stored = cxt.get_variable_value(name).value_or(String{});
        os::set_environment_variable(name, stored.view());
        cxt.mark_exported(name);
      }
    }
  }

  return 0;
}

} /* namespace shit */
