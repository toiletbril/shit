#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

#include <cstdio>

/* umask prints the current file-creation mask with no argument, octal by
   default and symbolic under -S, and sets it from an octal or a symbolic
   argument such as u=rwx,g=rx,o=. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-S] [mask]");

HELP_DESCRIPTION_DECL(
    "The umask builtin prints the current file creation mask when no operand "
    "is given, in octal by default and in symbolic form under -S, and sets the "
    "mask from an octal operand or a symbolic operand such as u=rwx,g=rx,o.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace {

constexpr u32 PERMISSION_BITS = 0777u;

/* The nine permission bits of a who group, shifted to its position. */
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

/* Render a mask as the symbolic form -S prints, listing for each group the
   permissions the mask leaves enabled, which is the complement of the masked
   bits. */
fn mask_to_symbolic(u32 mask) throws -> String
{
  const u32 allowed = (~mask) & PERMISSION_BITS;
  const char groups[] = {'u', 'g', 'o'};
  const u32 shifts[] = {6, 3, 0};
  let out = String{};
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

/* Apply a symbolic mask specification to the current mask, returning the new
   mask, or None when the spec is malformed. The clauses operate on the enabled
   permissions, the complement of the mask, the way chmod-style symbols do, then
   the result is complemented back into a mask. */
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
    /* An omitted who names every group, the way POSIX treats it. */
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

    /* The permission triplet is replicated into each named group's position. */
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

} /* namespace */

Umask::Umask() = default;

pure Builtin::Kind Umask::kind() const wontthrow { return Kind::Umask; }

cold i32 Umask::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  bool print_symbolic = false;
  Maybe<usize> operand_index;
  for (usize i = 1; i < args.count(); i++) {
    if (args[i] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);
    if (args[i] == "-S") {
      print_symbolic = true;
      continue;
    }
    operand_index = i;
    break;
  }

  if (!operand_index.has_value()) {
    const u32 mask = os::get_file_creation_mask();
    if (print_symbolic) {
      ec.print_to_stdout(mask_to_symbolic(mask) + "\n");
    } else {
      char buffer[8];
      std::snprintf(buffer, sizeof(buffer), "%04o", mask);
      ec.print_to_stdout(String{buffer} + "\n");
    }
    return 0;
  }

  let const &requested = args[*operand_index];

  /* A leading digit names an octal mask, anything else a symbolic spec, the way
     POSIX distinguishes the two operand forms. */
  if (!requested.is_empty() && requested[0] >= '0' && requested[0] <= '7') {
    let const parsed = utils::parse_octal_integer(requested);
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

} /* namespace shit */
