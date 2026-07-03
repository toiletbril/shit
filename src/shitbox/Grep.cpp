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

#if SHIT_PLATFORM_ISNT POSIX
static fn lower(char character) wontthrow -> char
{
  return (character >= 'A' && character <= 'Z')
             ? static_cast<char>(character - 'A' + 'a')
             : character;
}

static fn contains(StringView haystack, StringView needle,
                   bool should_ignore_case) wontthrow -> bool
{
  if (needle.length == 0) return true;
  if (needle.length > haystack.length) return false;

  if (!should_ignore_case) {
    usize start = 0;
    while (start + needle.length <= haystack.length) {
      let const found = haystack.substring(start).find_character(needle[0]);
      if (!found.has_value()) return false;
      start += *found;
      if (start + needle.length > haystack.length) return false;

      bool is_matched = true;
      for (usize k = 1; k < needle.length; k++)
        if (haystack[start + k] != needle[k]) {
          is_matched = false;
          break;
        }
      if (is_matched) return true;
      start++;
    }
    return false;
  }

  for (usize start = 0; start + needle.length <= haystack.length; start++) {
    bool is_matched = true;
    for (usize k = 0; k < needle.length; k++) {
      if (lower(haystack[start + k]) != lower(needle[k])) {
        is_matched = false;
        break;
      }
    }
    if (is_matched) return true;
  }

  return false;
}
#endif

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

#if SHIT_PLATFORM_IS POSIX
  let const pattern_text = String{cxt.scratch_allocator(), pattern};
  int compile_flags = REG_NOSUB;
  if (should_ignore_case) compile_flags |= REG_ICASE;

  regex_t compiled;
  const int compile_result =
      regcomp(&compiled, pattern_text.c_str(), compile_flags);
  if (compile_result != 0) {
    char error_text[256];
    regerror(compile_result, &compiled, error_text, sizeof(error_text));
    report_soft_shitbox_error(
        ec, cxt,
        "grep: " + String{cxt.scratch_allocator(), StringView{error_text}});
    return 2;
  }
  defer { regfree(&compiled); };
#endif

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
#if SHIT_PLATFORM_IS POSIX
      let const line_text = String{cxt.scratch_allocator(), body};
      let const is_match =
          regexec(&compiled, line_text.c_str(), 0, nullptr, 0) == 0;
#else
      let const is_match = contains(body, pattern, should_ignore_case);
#endif
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
