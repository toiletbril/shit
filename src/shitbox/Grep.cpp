#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-iv] pattern [file ...]");

HELP_DESCRIPTION_DECL(
    "The grep utility prints the lines of each file that contain the fixed "
    "string pattern, reading standard input when no file is given. The status "
    "is zero when a line matched and one when none did.");

FLAG(GREP_IGNORE_CASE, Bool, 'i', "", "Match without regard to letter case.");
FLAG(GREP_INVERT, Bool, 'v', "", "Print the lines that do not match.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

static fn lower(char c) wontthrow -> char
{
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/* Whether haystack contains needle as a substring, optionally folding letter
   case, the fixed-string match grep does without a regex engine. */
static fn contains(StringView haystack, StringView needle,
                   bool ignore_case) wontthrow -> bool
{
  if (needle.length == 0) return true;
  if (needle.length > haystack.length) return false;
  for (usize start = 0; start + needle.length <= haystack.length; start++) {
    bool matched = true;
    for (usize k = 0; k < needle.length; k++) {
      char a = haystack[start + k];
      char b = needle[k];
      if (ignore_case) {
        a = lower(a);
        b = lower(b);
      }
      if (a != b) {
        matched = false;
        break;
      }
    }
    if (matched) return true;
  }
  return false;
}

fn util_grep(const ExecContext &ec, EvalContext &cxt,
             const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) throw Error{"grep expects a pattern"};

  let const pattern = operands[0].view();
  let const ignore_case = FLAG_GREP_IGNORE_CASE.is_enabled();
  let const invert = FLAG_GREP_INVERT.is_enabled();

  ArrayList<StringView> sources{};
  if (operands.count() == 1)
    sources.push(StringView{"-"});
  else
    for (usize i = 1; i < operands.count(); i++)
      sources.push(operands[i].view());

  let const print_names = sources.count() > 1;
  let output = String{};
  bool any_match = false;
  i32 status = 0;
  for (const StringView &source : sources) {
    Maybe<String> content = read_named_or_stdin(ec, source);
    if (!content.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "grep: " + String{source} + ": " +
                                    os::last_system_error_message());
      status = 2;
      continue;
    }
    for (const StringView &line : split_keep_newlines(content->view())) {
      /* The newline is excluded from the match so a pattern does not have to
         account for it, but it is kept on the printed line. */
      let const had_newline = !line.is_empty() && line[line.length - 1] == '\n';
      let const body =
          had_newline ? line.substring_of_length(0, line.length - 1) : line;
      let const hit = contains(body, pattern, ignore_case);
      if (hit == invert) continue;
      any_match = true;
      if (print_names) {
        output += source;
        output += ':';
      }
      output += line;
      if (!had_newline) output += '\n';
    }
  }

  ec.print_to_stdout(output);
  if (status == 2) return 2;
  return any_match ? 0 : 1;
}

} /* namespace shitbox */

} /* namespace shit */
