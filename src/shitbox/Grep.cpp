#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-iv] pattern [file ...]");

HELP_DESCRIPTION_DECL(
    "The grep utility prints the lines of each file that match a pattern.");

FLAG(GREP_IGNORE_CASE, Bool, 'i', "", "Match without regard to letter case.");
FLAG(GREP_INVERT, Bool, 'v', "", "Print the lines that do not match.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Grep);

namespace shit {

namespace shitbox {

Grep::Grep() = default;

pure fn Grep::kind() const wontthrow -> Utility::Kind { return Kind::Grep; }

fn Grep::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const pattern = operands[0].view();
  let const should_ignore_case = FLAG_GREP_IGNORE_CASE.is_enabled();
  let const should_invert = FLAG_GREP_INVERT.is_enabled();

  os::compiled_regex compiled;
  if (os::compile_search_regex(pattern, should_ignore_case, compiled) !=
      os::regex_compile_result::Ok)
  {
    report_soft_shitbox_error(ec, cxt,
                              "grep: the pattern '" + operands[0] +
                                  "' is not a valid regex");
    return 2;
  }
  defer { os::free_regex(compiled); };

  ArrayList<StringView> sources{cxt.scratch_allocator()};
  if (operands.count() == 1)
    sources.push(StringView{"-"});
  else
    for (usize i = 1; i < operands.count(); i++)
      sources.push(operands[i].view());

  let const should_print_names = sources.count() > 1;
  let output = String{cxt.scratch_allocator()};
  bool has_any_match = false;
  i32 status = 0;
  for (let const &source : sources) {
    let const content = read_named_or_stdin(ec, source);
    if (os::INTERRUPT_REQUESTED) return 130;
    if (!content.has_value()) {
      report_soft_shitbox_error(
          ec, cxt,
          "grep: " + String{cxt.scratch_allocator(), source} + ": " +
              os::last_system_error_message());
      status = 2;
      continue;
    }
    for (let const &line : split_keep_newlines(content->view())) {
      let const has_newline = !line.is_empty() && line[line.length - 1] == '\n';
      let const body =
          has_newline ? line.substring_of_length(0, line.length - 1) : line;
      let const is_match = os::regex_matches(compiled, body);
      if (is_match == should_invert) continue;

      has_any_match = true;
      if (should_print_names) {
        output += source == "-" ? StringView{"(standard input)"} : source;
        output += ':';
      }
      output += line;
      if (!has_newline) output += '\n';
    }
  }

  ec.print_to_stdout(output);
  if (status == 2) return 2;

  return has_any_match ? 0 : 1;
}

} // namespace shitbox

} // namespace shit
