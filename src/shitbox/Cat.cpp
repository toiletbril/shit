#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n] [file ...]");

HELP_DESCRIPTION_DECL(
    "The cat utility writes each file to standard output, or standard input "
    "when no file is given or a file is named '-'. With -n it numbers every "
    "output line.");

FLAG(CAT_NUMBER, Bool, 'n', "", "Number every output line, starting at one.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Cat);

namespace shit {

namespace shitbox {

/* The cat -n prefix, the line number right-justified in six columns and a tab,
   the spacing GNU cat prints. */
static fn number_prefix(i64 line_number) throws -> String
{
  let const digits = utils::int_to_text(line_number, heap_allocator());
  String prefix{};
  for (usize i = digits.count(); i < 6; i++)
    prefix += ' ';
  prefix += digits.view();
  prefix += '\t';
  return prefix;
}

Cat::Cat() = default;

pure Utility::Kind Cat::kind() const wontthrow { return Kind::Cat; }

fn Cat::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  ArrayList<StringView> sources{};
  if (operands.is_empty())
    sources.push(StringView{"-"});
  else
    for (let const &operand : operands)
      sources.push(operand.view());

  let output = String{};
  i64 line_number = 1;
  i32 status = 0;
  for (let const &source : sources) {
    Maybe<String> content = read_named_or_stdin(ec, source);
    if (!content.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "cat: " + String{source} + ": " +
                                    os::last_system_error_message());
      status = 1;
      continue;
    }
    if (!FLAG_CAT_NUMBER.is_enabled()) {
      output += content->view();
      continue;
    }
    for (let const &line : split_keep_newlines(content->view())) {
      output += number_prefix(line_number);
      output += line;
      line_number++;
    }
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
