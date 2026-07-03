#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#include <cstdio>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-p] [-S] [mask]");

HELP_DESCRIPTION_DECL(
    "The umask builtin prints or sets the file creation mask.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
/* -p and -S are hand-parsed in execute, so these FLAG rows only feed the help
   text. */
FLAG(UMASK_REUSABLE, Bool, 'p', "",
     "Print the mask in a form reusable as input.");
FLAG(UMASK_SYMBOLIC, Bool, 'S', "",
     "Print the mask in symbolic form, the u=rwx,g=rx,o=rx style.");

REGISTER_BUILTIN_FLAGS(Umask);

namespace shit {

namespace {

constexpr u32 PERMISSION_BITS = 0777u;

pure fn group_mask(char who) wontthrow -> u32
{
  switch (who) {
  case 'u': return 0700u;
  case 'g': return 0070u;
  case 'o': return 0007u;
  case 'a': return 0777u;
  default: return 0;
  }
}

fn mask_to_symbolic(u32 mask, Allocator allocator) throws -> String
{
  const u32 allowed = (~mask) & PERMISSION_BITS;
  const char groups[] = {'u', 'g', 'o'};
  const u32 shifts[] = {6, 3, 0};
  let out = String{allocator};
  for (usize g = 0; g < 3; g++) {
    if (g > 0) out.push(',');
    out.push(groups[g]);
    out.push('=');
    const u32 bits = (allowed >> shifts[g]) & 7u;
    if (bits & 4u) out.push('r');
    if (bits & 2u) out.push('w');
    if (bits & 1u) out.push('x');
  }
  return out;
}

/* The clauses operate on the complement of the mask, then the result is
   complemented back into a mask. */
fn apply_symbolic_mask(StringView spec, u32 current_mask) throws -> Maybe<u32>
{
  u32 allowed = (~current_mask) & PERMISSION_BITS;
  usize i = 0;
  while (i < spec.length) {
    u32 who = 0;
    while (i < spec.length && (spec[i] == 'u' || spec[i] == 'g' ||
                               spec[i] == 'o' || spec[i] == 'a'))
    {
      who |= group_mask(spec[i]);
      i++;
    }
    if (who == 0) who = PERMISSION_BITS;

    if (i >= spec.length) return None;
    const char op = spec[i];
    if (op != '+' && op != '-' && op != '=') return None;
    i++;

    u32 permission = 0;
    while (i < spec.length &&
           (spec[i] == 'r' || spec[i] == 'w' || spec[i] == 'x'))
    {
      if (spec[i] == 'r') permission |= 4u;
      if (spec[i] == 'w') permission |= 2u;
      if (spec[i] == 'x') permission |= 1u;
      i++;
    }

    u32 permission_bits = 0;
    if (who & 0700u) permission_bits |= permission << 6;
    if (who & 0070u) permission_bits |= permission << 3;
    if (who & 0007u) permission_bits |= permission;

    if (op == '=')
      allowed = (allowed & ~who) | permission_bits;
    else if (op == '+')
      allowed |= permission_bits;
    else
      allowed &= ~permission_bits;

    if (i < spec.length) {
      if (spec[i] != ',') return None;
      i++;
    }
  }
  return (~allowed) & PERMISSION_BITS;
}

} // namespace

Umask::Umask() = default;

pure fn Umask::kind() const wontthrow -> Builtin::Kind { return Kind::Umask; }

cold i32 Umask::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  bool should_print_symbolic = false;
  bool should_print_reusable = false;
  Maybe<usize> operand_index;

  usize i = 1;
  for (; i < args.count(); i++) {
    const StringView arg = args[i].view();
    if (arg == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

    if (arg.length < 2 || arg[0] != '-') {
      break;
    }

    if (arg == "--") {
      i++;
      break;
    }

    for (usize c = 1; c < arg.length; c++) {
      switch (arg[c]) {
      case 'S': should_print_symbolic = true; break;
      case 'p': should_print_reusable = true; break;
      default: {
        let invalid = String{cxt.scratch_allocator()};
        invalid += '-';
        invalid += arg[c];
        report_soft_builtin_error(
            ec, cxt, "'" + invalid + "' is not a valid umask option");
        return 2;
      }
      }
    }
  }

  if (i < args.count()) operand_index = i;

  if (!operand_index.has_value()) {
    const u32 mask = os::get_file_creation_mask();
    let out = String{cxt.scratch_allocator()};
    if (should_print_reusable)
      out += should_print_symbolic ? "umask -S " : "umask ";

    if (should_print_symbolic) {
      out += mask_to_symbolic(mask, cxt.scratch_allocator()).view();
    } else {
      char buffer[8];
      std::snprintf(buffer, sizeof(buffer), "%04o", mask);
      out += buffer;
    }
    out.push('\n');

    ec.print_to_stdout(out);
    return 0;
  }

  let const &requested = args[*operand_index];

  LOG(Debug, "umask setting the file creation mask from '%s'",
      requested.c_str());

  if (!requested.is_empty() && requested[0] >= '0' && requested[0] <= '7') {
    let const parsed = utils::parse_integer_in_base(requested, int_base::octal);
    if (parsed.is_error())
      throw Error{"Unable to set the file creation mask because '" + requested +
                  "' is not a valid octal mask"};
    os::set_file_creation_mask(static_cast<u32>(parsed.value()));
    return 0;
  }

  let const new_mask =
      apply_symbolic_mask(requested.view(), os::get_file_creation_mask());
  if (!new_mask.has_value())
    throw Error{"Unable to set the file creation mask because '" + requested +
                "' is not a valid symbolic mask"};
  os::set_file_creation_mask(*new_mask);

  return 0;
}

} // namespace shit
