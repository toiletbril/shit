#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* -A makes a name an associative array and -a makes it indexed, so a later
   subscript assignment routes to the right store. -i marks a name an integer
   so every assignment to it evaluates as arithmetic. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-aAfFgilnprtux] [+i] [name[=value] ...]");

HELP_DESCRIPTION_DECL(
    "The declare builtin, also spelled typeset, declares variables and sets "
    "their attributes. A plus before a letter removes the attribute where "
    "the removal is backed, the +i form. In bash mode it backs the array and "
    "associative types.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
/* The attribute letters are hand-parsed in execute, so these FLAG rows only
   feed the help text and never the parser. */
FLAG(DECLARE_INDEXED, Bool, 'a', "", "Declare an indexed array.");
FLAG(DECLARE_ASSOCIATIVE, Bool, 'A', "", "Declare an associative array.");
FLAG(DECLARE_FUNCTIONS, Bool, 'f', "",
     "Restrict to functions and print their recorded definitions.");
FLAG(DECLARE_FUNCTION_NAMES, Bool, 'F', "",
     "Print only the names of defined functions, existence by status.");
FLAG(DECLARE_GLOBAL, Bool, 'g', "", "Accepted without effect.");
FLAG(DECLARE_INTEGER, Bool, 'i', "",
     "Mark an integer whose every assignment evaluates as arithmetic, "
     "removed by +i.");
FLAG(DECLARE_LOWERCASE, Bool, 'l', "", "Accepted without effect.");
FLAG(DECLARE_NAMEREF, Bool, 'n', "", "Accepted without effect.");
FLAG(DECLARE_PRINT, Bool, 'p', "", "Print the matching declarations.");
FLAG(DECLARE_READONLY, Bool, 'r', "", "Accepted without effect.");
FLAG(DECLARE_TRACE, Bool, 't', "", "Accepted without effect.");
FLAG(DECLARE_UPPERCASE, Bool, 'u', "", "Accepted without effect.");
FLAG(DECLARE_EXPORT, Bool, 'x', "", "Mark the variable for the environment.");

REGISTER_BUILTIN_FLAGS(Declare);

namespace shit {

Declare::Declare() = default;

pure fn Declare::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Declare;
}

fn Declare::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  bool should_make_associative = false;
  bool should_make_indexed = false;
  bool should_export = false;
  bool should_print = false;
  bool should_mark_integer_attribute = false;
  bool should_unmark_integer_attribute = false;
  bool should_mark_readonly = false;
  bool should_restrict_to_functions = false;
  bool should_print_function_names_only = false;

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
      case 'A': should_make_associative = true; break;
      case 'a': should_make_indexed = true; break;
      case 'x': should_export = true; break;
      case 'p': should_print = true; break;
      case 'i':
        if (is_remove_form)
          should_unmark_integer_attribute = true;
        else
          should_mark_integer_attribute = true;
        break;
      case 'f': should_restrict_to_functions = true; break;
      case 'F':
        should_restrict_to_functions = true;
        should_print_function_names_only = true;
        break;
      case 'r':
        /* bash refuses to drop the read-only attribute, so the +r form is left
           as a silent no-op rather than unmarking. */
        if (!is_remove_form) should_mark_readonly = true;
        break;
      /* The remaining attribute letters carry no backing behavior yet and are
         accepted so a script that sets them keeps running. */
      case 'g':
      case 'l':
      case 'u':
      case 'n':
      case 't': break;
      default: {
        let invalid = String{};
        invalid += arg[0];
        invalid += arg[c];
        throw Error{"'" + invalid + "' is not a valid declare option"};
      }
      }
    }
  }

  /* A missing name turns the status to 1, silently for -F the way bash answers
     an existence probe, with a message for -f. */
  if (should_restrict_to_functions) {
    i32 status = 0;
    if (i >= args.count()) {
      for (let const &name : cxt.sorted_function_names()) {
        let line = String{};
        if (should_print_function_names_only) {
          line += "declare -f ";
          line.append(name.view());
        } else if (const String *source = cxt.find_function_source(name.view());
                   source != nullptr)
        {
          line.append(source->view());
        }
        line += '\n';
        ec.print_to_stdout(line.view());
      }
      return 0;
    }
    for (; i < args.count(); i++) {
      const StringView name = args[i].view();
      if (cxt.find_function(name) == nullptr) {
        if (!should_print_function_names_only)
          report_soft_builtin_error(
              ec, cxt, StringView{"'"} + name + "' is not a function");
        status = 1;
        continue;
      }
      if (should_print_function_names_only) {
        let line = String{name};
        line += '\n';
        ec.print_to_stdout(line.view());
      } else if (const String *source = cxt.find_function_source(name);
                 source != nullptr)
      {
        if (!source->is_empty()) {
          let line = String{source->view()};
          line += '\n';
          ec.print_to_stdout(line.view());
        }
      }
    }
    return status;
  }

  /* declare -p prints in the bash syntax so a script can reload the state. An
     unknown name is an error. */
  if (should_print) {
    i32 status = 0;
    for (; i < args.count(); i++) {
      const StringView name = args[i].view();
      if (const ArrayList<String> *elements = cxt.lookup_indexed_array(name);
          elements != nullptr)
      {
        let line = String{"declare -a"};
        if (cxt.is_integer_variable(name)) line += 'i';
        line += ' ';
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
        let line = String{"declare -A"};
        if (cxt.is_integer_variable(name)) line += 'i';
        line += ' ';
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
      } else if (cxt.is_integer_variable(name)) {
        /* An integer-marked name with no value yet still has the attribute, so
           it prints without the =value tail the way bash does. */
        let line = String{"declare -i"};
        if (os::get_environment_variable(name).has_value()) line += 'x';
        line += ' ';
        line.append(name);
        line += '\n';
        ec.print_to_stdout(line.view());
      } else {
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + name + "' is not defined");
        status = 1;
      }
    }
    return status;
  }

  for (; i < args.count(); i++) {
    const StringView operand = args[i].view();
    let const equals = operand.find_character('=');
    let name =
        equals.has_value() ? operand.substring_of_length(0, *equals) : operand;
    const StringView value =
        equals.has_value() ? operand.substring(*equals + 1) : StringView{};

    /* process_args passes a declare append through as name+=value, so a
       trailing plus on the name marks the append and is stripped before the
       attributes apply. */
    let const is_append =
        equals.has_value() && !name.is_empty() && name[name.count() - 1] == '+';
    if (is_append) name = name.substring_of_length(0, name.count() - 1);

    /* A subscripted operand such as a[0]=5 declares the base name's array and
       assigns the element, the way bash treats declare a[i]=v, so the
       attribute marks land on the base name rather than the bracketed text. */
    let const bracket = name.find_character('[');
    let const has_subscript = bracket.has_value() && !name.is_empty() &&
                              name[name.count() - 1] == ']';
    const StringView subscript =
        has_subscript ? name.substring_of_length(*bracket + 1,
                                                 name.count() - *bracket - 2)
                      : StringView{};
    if (has_subscript) name = name.substring_of_length(0, *bracket);

    /* The attribute applies before the assignment, so declare -i x+=3 already
       adds on this command the way bash applies the integer mark first. */
    if (should_mark_integer_attribute) cxt.mark_integer(name);
    if (should_unmark_integer_attribute) cxt.unmark_integer(name);

    LOG(All, "declare applying attributes to '%.*s'",
        static_cast<int>(name.length), name.data);

    if (has_subscript && equals.has_value()) {
      cxt.assign_array_element(name, subscript, value, is_append);
    } else if (should_make_associative) {
      LOG(All, "declare making '%.*s' an associative array",
          static_cast<int>(name.length), name.data);
      cxt.declare_associative_array(name);
    } else if (should_make_indexed) {
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
      if (should_export) {
        LOG(All, "declare exporting '%.*s' to the environment",
            static_cast<int>(name.length), name.data);
        cxt.record_environment_change(name);
        /* The store may have rewritten the value, an integer name stores the
           arithmetic result, so the environment receives the stored value
           rather than the raw text. */
        let const stored = cxt.get_variable_value(name).value_or(String{});
        os::set_environment_variable(name, stored.view());
        cxt.mark_exported(name);
      }
    }

    /* The read-only mark applies after the assignment, so declare -r v=1 stores
       the value first and then locks it, the way bash rejects only a later
       write rather than the declaration's own assignment. */
    if (should_mark_readonly) cxt.mark_readonly(name);
  }

  return 0;
}

} // namespace shit
