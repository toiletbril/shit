#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-d] set1 [set2]");

HELP_DESCRIPTION_DECL(
    "The tr utility reads standard input and translates the bytes in set1 to "
    "the matching bytes in set2, or with -d deletes the bytes in set1. A range "
    "such as a-z, or a descending one such as z-a, expands to every byte "
    "between its ends.");

FLAG(TR_DELETE, Bool, 'd', "",
     "Delete the bytes in set1 instead of translating.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Tr);

namespace shit {

namespace shitbox {

/* A descending range such as c-a expands in reverse to cba the way GNU tr reads
   it. The bounds are read as unsigned bytes and the walk runs over an int, so a
   range that touches the 0 or 255 edge does not overflow a char. */
static fn expand_set(StringView set) throws -> String
{
  String expanded{};
  usize i = 0;
  while (i < set.length) {
    if (i + 2 < set.length && set[i + 1] == '-') {
      let const low = static_cast<int>(static_cast<unsigned char>(set[i]));
      let const high = static_cast<int>(static_cast<unsigned char>(set[i + 2]));
      if (low <= high)
        for (int c = low; c <= high; c++)
          expanded.push(static_cast<char>(c));
      else
        for (int c = low; c >= high; c--)
          expanded.push(static_cast<char>(c));
      i += 3;
    } else {
      expanded.push(set[i]);
      i++;
    }
  }
  return expanded;
}

Tr::Tr() = default;

pure Utility::Kind Tr::kind() const wontthrow { return Kind::Tr; }

fn Tr::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const is_deleting = FLAG_TR_DELETE.is_enabled();
  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  if (!is_deleting && operands.count() < 2) {
    throw Error{"tr expects two sets unless -d is given"};
  }

  let const set1 = expand_set(operands[0].view());
  let const set2 = is_deleting ? String{} : expand_set(operands[1].view());

  /* The first occurrence of a byte in set1 wins. A byte past the end of set2
     maps to its last byte, the way tr pads the shorter set with its final
     character. */
  static constexpr usize BYTE_VALUE_COUNT = 256;
  bool is_in_set1[BYTE_VALUE_COUNT] = {};
  unsigned char translation[BYTE_VALUE_COUNT];
  for (usize i = 0; i < BYTE_VALUE_COUNT; i++)
    translation[i] = static_cast<unsigned char>(i);

  /* set2 is indexed by the distinct ordinal of the set1 byte, not its raw scan
     position, so a duplicate byte earlier in set1 does not shift every later
     mapping the way a raw position would. */
  usize distinct_count = 0;
  for (usize i = 0; i < set1.count(); i++) {
    let const from = static_cast<unsigned char>(set1.view()[i]);
    if (is_in_set1[from]) continue;
    is_in_set1[from] = true;
    if (!is_deleting && set2.count() > 0) {
      let const index =
          distinct_count < set2.count() ? distinct_count : set2.count() - 1;
      translation[from] = static_cast<unsigned char>(set2.view()[index]);
    }

    distinct_count++;
  }

  let const input = read_fd_to_string(ec.in_fd.value_or(SHIT_STDIN));
  /* A Ctrl-C during the read returns 130 rather than freezing the utility. */
  if (os::INTERRUPT_REQUESTED) return 130;
  let output = String{};
  output.reserve(input.count());
  for (usize i = 0; i < input.count(); i++) {
    let const c = static_cast<unsigned char>(input.view()[i]);
    if (!is_in_set1[c]) {
      output.push(static_cast<char>(c));
      continue;
    }
    if (is_deleting) continue;
    output.push(static_cast<char>(translation[c]));
  }

  ec.print_to_stdout(output);
  return 0;
}

} // namespace shitbox

} // namespace shit
