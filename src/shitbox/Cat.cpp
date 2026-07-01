#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n] [file ...]");

HELP_DESCRIPTION_DECL(
    "The cat utility writes each file to standard output.");

FLAG(CAT_NUMBER, Bool, 'n', "", "Number every output line, starting at one.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Cat);

namespace shit {

namespace shitbox {

static fn number_prefix(i64 line_number, Allocator allocator) throws -> String
{
  let const digits = String::from(line_number, allocator);
  String prefix{allocator};
  for (usize i = digits.count(); i < 6; i++)
    prefix += ' ';
  prefix += digits.view();
  prefix += '\t';
  return prefix;
}

Cat::Cat() = default;

pure fn Cat::kind() const wontthrow -> Utility::Kind { return Kind::Cat; }

fn Cat::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  let output = String{cxt.scratch_allocator()};
  i64 line_number = 1;
  i32 status = 0;
  for (let const &source : sources) {
    let const content = read_named_or_stdin(ec, source);
    if (os::INTERRUPT_REQUESTED) return 130;
    if (!content.has_value()) {
      report_soft_shitbox_error(
          ec, cxt,
          "cat: " + String{cxt.scratch_allocator(), source} + ": " +
              os::last_system_error_message());
      status = 1;
      continue;
    }
    if (!FLAG_CAT_NUMBER.is_enabled()) {
      output += content->view();
      continue;
    }
    for (let const &line : split_keep_newlines(content->view())) {
      output += number_prefix(line_number, cxt.scratch_allocator());
      output += line;
      line_number++;
    }
  }

  ec.print_to_stdout(output);
  return status;
}

} // namespace shitbox

} // namespace shit
