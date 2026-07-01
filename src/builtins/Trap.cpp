#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#include <cctype>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[action condition ...]");

HELP_DESCRIPTION_DECL(
    "The trap builtin sets the action to run for each named condition, resets "
    "those conditions to their defaults when the action is a lone dash, and "
    "resets a single named condition given with no action. With no argument it "
    "lists the set traps, and the EXIT action runs when the shell ends.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(TRAP_PRINT, Bool, 'p', "", "Print the set traps in a reusable form.");

REGISTER_BUILTIN_FLAGS(Trap);

namespace shit {

Trap::Trap() = default;

pure fn Trap::kind() const wontthrow -> Builtin::Kind { return Kind::Trap; }

namespace {

/* Normalize a condition name to its bare upper-case form, so SIGINT, sigint,
   int, and the number 2 all name the same condition, and 0 names EXIT. A trap
   set by name and cleared or listed by number must resolve to one key, so a
   bare number maps through the os signal name table the way dash lists it. */
String normalize_condition(StringView raw, Allocator allocator) throws
{
  let name = String{allocator};
  for (usize i = 0; i < raw.count(); i++)
    name.push(static_cast<char>(toupper(static_cast<unsigned char>(raw[i]))));
  if (name.starts_with("SIG") && name.count() > 3) {
    name = String{allocator, name.substring(3)};
  }
  if (name == "0") return String{allocator, "EXIT"};

  /* A condition written as a bare number names a signal, so it folds to the
     same name the name form yields. The number 0 already became EXIT above. */
  if (name.view().is_all_decimal_digits()) {
    let const parsed = utils::parse_decimal_integer(name.view());
    if (!parsed.is_error()) {
      if (let const signal_name =
              os::signal_name_from_number(static_cast<i32>(parsed.value())))
        return *signal_name;
    }
  }
  return name;
}

/* Render a stored condition for the listing. bash prints a signal with the SIG
   prefix it strips on input, so SIGINT round-trips, while EXIT and the other
   non-signal conditions print bare. The default and sh moods keep the bare name
   the shell has always listed. */
String format_listed_condition(StringView condition, bool with_sig_prefix,
                               Allocator allocator) throws
{
  if (with_sig_prefix && os::signal_number_from_name(condition).has_value()) {
    let prefixed = String{allocator, "SIG"};
    prefixed += condition;
    return prefixed;
  }
  return String{allocator, condition};
}

} // namespace

fn Trap::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (!cxt.is_posix_mode() && args.count() == 2 &&
      (args[1] == "-l" || args[1] == "--list"))
  {
    ec.print_to_stdout(shitbox::format_signal_list());
    return 0;
  }

  /* A bare trap, and the -p form, list the set traps. -p may name the
     conditions to list, so only those are printed when it carries operands. The
     -p flag is a bash extension, so the sh mood takes -p as an ordinary action
     the way dash does. */
  let const is_print_form =
      !cxt.is_posix_mode() && args.count() >= 2 && args[1] == "-p";
  if (args.count() == 1 || is_print_form) {
    let const with_sig_prefix = cxt.is_bash_compatible();
    let const has_filter = is_print_form && args.count() > 2;

    let out = String{cxt.scratch_allocator()};
    cxt.traps().for_each([&](StringView condition, const String &action) {
      if (has_filter) {
        let was_requested = false;
        for (usize i = 2; i < args.count(); i++)
          if (normalize_condition(args[i], cxt.scratch_allocator()).view() ==
              condition)
          {
            was_requested = true;
            break;
          }
        if (!was_requested) return;
      }

      out += "trap -- '";
      out += action;
      out += "' ";
      out += format_listed_condition(condition, with_sig_prefix,
                                     cxt.scratch_allocator());
      out += '\n';
    });
    ec.print_to_stdout(out);
    return 0;
  }

  ASSERT(args.count() > 1);

  /* With a single operand there is no action, so the operand names a condition
     to reset to its default, as POSIX specifies for 'trap SIG' and 'trap NUM'.
     With two or more operands the first is the action and the rest are the
     conditions it applies to. */
  if (args.count() == 2) {
    let const condition = normalize_condition(args[1], cxt.scratch_allocator());
    LOG(Info, "trap resetting condition '%s' to its default",
        condition.c_str());
    cxt.remove_trap(condition);
    return 0;
  }

  let const &action = args[1];
  let const is_reset = action == "-";

  for (usize i = 2; i < args.count(); i++) {
    let const condition = normalize_condition(args[i], cxt.scratch_allocator());
    LOG(Info, "trap %s action for signal '%s'",
        is_reset ? "resetting the" : "setting", condition.c_str());
    if (is_reset)
      cxt.remove_trap(condition);
    else
      cxt.set_trap(condition, action);
  }

  return 0;
}

} // namespace shit
