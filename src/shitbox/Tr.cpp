#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-d] set1 [set2]");

HELP_DESCRIPTION_DECL(
    "The tr utility reads standard input and translates the bytes in set1 to "
    "the matching bytes in set2, or with -d deletes the bytes in set1. A range "
    "such as a-z expands to every byte between its ends.");

FLAG(TR_DELETE, Bool, 'd', "",
     "Delete the bytes in set1 instead of translating.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

/* Expand a set with a-z ranges into the flat run of bytes it names, so a-c
   becomes abc. */
static fn expand_set(StringView set) throws -> String
{
  String expanded{};
  usize i = 0;
  while (i < set.length) {
    if (i + 2 < set.length && set[i + 1] == '-' && set[i] <= set[i + 2]) {
      for (char c = set[i]; c <= set[i + 2]; c++)
        expanded.push(c);
      i += 3;
    } else {
      expanded.push(set[i]);
      i++;
    }
  }
  return expanded;
}

fn util_tr(const ExecContext &ec, EvalContext &cxt,
           const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) {
    print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                    FLAG_LIST);
    return 0;
  }

  let const deleting = FLAG_TR_DELETE.is_enabled();
  if (operands.is_empty()) throw Error{"tr expects a set of bytes"};
  if (!deleting && operands.count() < 2)
    throw Error{"tr expects two sets unless -d is given"};

  let const set1 = expand_set(operands[0].view());
  let const set2 = deleting ? String{} : expand_set(operands[1].view());

  let const input = read_fd_to_string(ec.in_fd.value_or(SHIT_STDIN));
  let output = String{};
  for (usize i = 0; i < input.count(); i++) {
    let const c = input.view()[i];
    let const found = set1.find_character(c);
    if (!found.has_value()) {
      output.push(c);
      continue;
    }
    if (deleting) continue;
    /* A byte past the end of set2 maps to its last byte, the way tr pads the
       shorter set with its final character. */
    let const index = *found < set2.count() ? *found : set2.count() - 1;
    output.push(set2.view()[index]);
  }

  ec.print_to_stdout(output);
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
