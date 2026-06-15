#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-lwc] [file ...]");

HELP_DESCRIPTION_DECL(
    "The wc utility counts the lines, words, and bytes of each file, reading "
    "standard input when no file is given. With no flag it prints all three "
    "counts.");

FLAG(WC_LINES, Bool, 'l', "", "Print the newline count.");
FLAG(WC_WORDS, Bool, 'w', "", "Print the word count.");
FLAG(WC_BYTES, Bool, 'c', "", "Print the byte count.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Wc);

namespace shit {

namespace shitbox {

static fn is_blank(char c) wontthrow -> bool
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

/* One file's count line, the selected fields right-justified in eight columns
   each, with the name appended when one is given. */
static fn format_counts(u64 lines, u64 words, u64 bytes, bool should_show_lines,
                        bool should_show_words, bool should_show_bytes,
                        StringView name) throws -> String
{
  let const do_field = [](u64 value) throws -> String {
    let const digits = utils::uint_to_text(value);
    String padded{};
    for (usize i = digits.count(); i < 8; i++)
      padded += ' ';
    padded += digits.view();
    return padded;
  };

  String line{};
  if (should_show_lines) line += do_field(lines);
  if (should_show_words) line += do_field(words);
  if (should_show_bytes) line += do_field(bytes);

  if (!name.is_empty()) {
    line += ' ';
    line += name;
  }

  line += '\n';
  return line;
}

Wc::Wc() = default;

pure Utility::Kind Wc::kind() const wontthrow { return Kind::Wc; }

fn Wc::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  bool should_show_lines = FLAG_WC_LINES.is_enabled();
  bool should_show_words = FLAG_WC_WORDS.is_enabled();
  bool should_show_bytes = FLAG_WC_BYTES.is_enabled();
  if (!should_show_lines && !should_show_words && !should_show_bytes) {
    should_show_lines = true;
    should_show_words = true;
    should_show_bytes = true;
  }

  ArrayList<StringView> sources{};
  if (operands.is_empty())
    sources.push(StringView{"-"});
  else
    for (const String &operand : operands)
      sources.push(operand.view());

  let output = String{};
  u64 total_lines = 0;
  u64 total_words = 0;
  u64 total_bytes = 0;
  i32 status = 0;
  for (const StringView &source : sources) {
    Maybe<String> content = read_named_or_stdin(ec, source);
    if (!content.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "wc: " + String{source} + ": " +
                                    os::last_system_error_message());
      status = 1;
      continue;
    }
    u64 lines = 0;
    u64 words = 0;
    u64 bytes = content->count();
    bool is_in_word = false;
    for (usize i = 0; i < content->count(); i++) {
      let const c = content->view()[i];
      if (c == '\n') lines++;
      if (is_blank(c)) {
        is_in_word = false;
      } else if (!is_in_word) {
        is_in_word = true;
        words++;
      }
    }

    total_lines += lines;
    total_words += words;
    total_bytes += bytes;

    let const name = source == "-" ? StringView{} : source;
    output += format_counts(lines, words, bytes, should_show_lines,
                            should_show_words, should_show_bytes, name);
  }

  if (sources.count() > 1)
    output += format_counts(total_lines, total_words, total_bytes,
                            should_show_lines, should_show_words,
                            should_show_bytes, StringView{"total"});

  ec.print_to_stdout(output);
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
