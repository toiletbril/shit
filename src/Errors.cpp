#include "Errors.hpp"

#include "Debug.hpp"
#include "ErrorOr.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* TODO: Print proper offset and context for UTF-8. */

namespace shit {

/* ANSI SGR sequences matching clang's diagnostic palette. The reset clears every
   attribute a colored span set, and the rest open a bold colored span. */
namespace ansi {
static const StringView RESET = "\x1b[0m";
static const StringView BOLD = "\x1b[1m";
static const StringView BOLD_RED = "\x1b[1;31m";
static const StringView BOLD_MAGENTA = "\x1b[1;35m";
static const StringView BOLD_CYAN = "\x1b[1;36m";
static const StringView BOLD_GREEN = "\x1b[1;32m";
} /* namespace ansi */

/* The SGR codes wrapping one diagnostic. Each field is empty when color is off,
   so the same render code appends them unconditionally and emits nothing on the
   plain path. The severity code follows clang, where an error is red, a warning
   is magenta, and a note or trace is cyan. */
struct diagnostic_color
{
  StringView severity{};
  StringView location{};
  StringView message{};
  StringView caret{};
  StringView reset{};
};

/* Whether diagnostics may carry color, decided fresh at render time so a
   redirected stderr never gains escapes. Color is on only when stderr is a
   terminal, NO_COLOR is unset or empty, and TERM is not dumb. */
cold static fn should_color_diagnostics() throws -> bool
{
  if (!os::is_stderr_a_tty()) return false;

  if (let const no_color = os::get_environment_variable("NO_COLOR");
      no_color.has_value() && !no_color->is_empty())
    return false;

  if (let const term = os::get_environment_variable("TERM");
      term.has_value() && term->view() == StringView{"dumb"})
    return false;

  return true;
}

/* The color codes for a diagnostic of this severity, or all empty StringViews
   when color is off. The severity word selects the severity hue, since the
   reporting code reads the word from the object rather than its concrete type.
 */
cold static fn diagnostic_colors_for(StringView severity_word) throws
    -> diagnostic_color
{
  if (!should_color_diagnostics()) return diagnostic_color{};

  StringView severity = ansi::BOLD_CYAN;
  if (severity_word == StringView{"Error"})
    severity = ansi::BOLD_RED;
  else if (severity_word == StringView{"Warning"})
    severity = ansi::BOLD_MAGENTA;

  return diagnostic_color{severity, ansi::BOLD, ansi::BOLD, ansi::BOLD_GREEN,
                          ansi::RESET};
}

/* The line a byte falls on and the offset of the newline that starts it. */
struct precise_location
{
  usize line_number;
  usize last_newline_location;
};

/* A per-source index that turns the line and column lookup from a scan over the
   whole prefix into a binary search. Without it a warning deep in the file costs
   time proportional to its byte offset, so N warnings cost N squared. The shell
   reuses one source for a whole analysis pass, so a single cached entry keyed on
   the source pointer and length serves every located message in that pass. */
class SourceLineIndex
{
public:
  SourceLineIndex()
      : m_newline_offsets(heap_allocator()),
        m_codepoints_before_newline(heap_allocator())
  {}

  /* Build the index for this source when it differs from the cached one. The
     entry holds, for each newline, its byte offset and the code point count of
     the bytes before it. */
  fn ensure_built_for(StringView source) throws -> void
  {
    if (m_source_data == source.data && m_source_length == source.count())
      return;

    m_source_data = source.data;
    m_source_length = source.count();
    m_newline_offsets.clear();
    m_codepoints_before_newline.clear();

    usize codepoints = 0;
    for (usize i = 0; i < source.count(); i++) {
      /* A byte that is not a UTF-8 continuation byte starts a code point. */
      if ((static_cast<u8>(source[i]) & 0xC0) != 0x80) codepoints++;
      if (source[i] == '\n') {
        m_newline_offsets.push(i);
        m_codepoints_before_newline.push(codepoints - 1);
      }
    }
  }

  /* The line number and the offset of the newline that starts it, matching the
     prefix scan it replaces. The line number counts newlines strictly before
     the byte, and the newline offset is the last such newline, or zero when the
     byte is on the first line. */
  pure fn locate(usize byte_position) const wontthrow -> precise_location
  {
    const usize newlines_before = count_newlines_before(byte_position);
    const usize last_newline_location =
        newlines_before == 0 ? 0 : m_newline_offsets[newlines_before - 1];
    return precise_location{newlines_before, last_newline_location};
  }

  /* The code point count of the bytes in the half open range before the byte,
     the value the prefix utf8 scan produced. It sums the code points up to the
     line start and the code points within the line up to the byte. */
  pure fn codepoints_before(StringView source,
                            usize byte_position) const wontthrow -> usize
  {
    const usize newlines_before = count_newlines_before(byte_position);
    if (newlines_before == 0)
      return toiletline::utf8_strnlen(source.data, byte_position);

    const usize newline_offset = m_newline_offsets[newlines_before - 1];
    const usize codepoints_through_newline =
        m_codepoints_before_newline[newlines_before - 1] + 1;
    return codepoints_through_newline +
           toiletline::utf8_strnlen(source.data + newline_offset + 1,
                                    byte_position - (newline_offset + 1));
  }

private:
  /* The count of newlines at a byte offset strictly less than the position,
     found by binary search over the sorted newline offsets. */
  pure fn count_newlines_before(usize byte_position) const wontthrow -> usize
  {
    usize low = 0;
    usize high = m_newline_offsets.count();
    while (low < high) {
      const usize mid = low + (high - low) / 2;
      if (m_newline_offsets[mid] < byte_position)
        low = mid + 1;
      else
        high = mid;
    }
    return low;
  }

  const char *m_source_data{nullptr};
  usize m_source_length{0};
  ArrayList<usize> m_newline_offsets;
  ArrayList<usize> m_codepoints_before_newline;
};

/* One index reused across every located message in a pass, since the pass works
   over a single source at a time. */
static SourceLineIndex SOURCE_LINE_INDEX{};

cold static fn calc_precise_position(StringView source,
                                     usize byte_position) throws
    -> precise_location
{
  ASSERT(byte_position <= source.count(),
         "byte position: %zu, source length: %zu", byte_position,
         source.count());

  SOURCE_LINE_INDEX.ensure_built_for(source);
  return SOURCE_LINE_INDEX.locate(byte_position);
}

template <class T>
cold static fn number_string_length(T n) throws -> usize
{
  usize len = 0;
  while (n > 0) {
    len++;
    n /= 10;
  }
  return len;
}

cold static fn get_context_pointing_to(StringView source, usize byte_position,
                                       usize byte_count, usize line_number,
                                       usize last_newline_location,
                                       usize unicode_position,
                                       Maybe<StringView> message,
                                       const diagnostic_color &color) throws
    -> String
{
  usize start_offset = byte_position - last_newline_location;

  /* If we have a newline before, start_offset points to this newline. Get rid
   * of it. */
  if (last_newline_location != 0 && start_offset > 0) start_offset--;

  usize line_byte_count = 0;

  while (byte_position - start_offset + line_byte_count < source.count() &&
         source[byte_position - start_offset + line_byte_count] != '\n')
  {
    line_byte_count++;
  }

  ASSERT(byte_position - start_offset + line_byte_count == source.count() ||
         source[byte_position - start_offset + line_byte_count] == '\n');

  /* Add spacer before line number. */
  String msg{};
  for (usize i = 0; i < sub_sat(6, number_string_length(line_number + 1)); i++)
  {
    msg += ' ';
  }

  msg += utils::uint_to_text(line_number + 1) + " |  ";

  /* Line that caused the error. */
  let const context =
      source.substring_of_length(byte_position - start_offset, line_byte_count);

  /* We don't need accidental newlines in the middle of the context.
   * *pulls hair out* */
  ASSERT(!context.find_character('\n').has_value(),
         "'%s', start: %zu, end: %zu", context.data, start_offset,
         line_byte_count);

  /* A tab is one byte but renders wide, so the caret below, padded by character
     count, would land short of the token. Each tab is expanded to a fixed width
     here, and the padding adds the same width per tab, so the caret lines up.
   */
  static constexpr usize TAB_WIDTH = 4;
  for (usize i = 0; i < context.count(); i++) {
    if (context[i] == '\t')
      for (usize t = 0; t < TAB_WIDTH; t++)
        msg += ' ';
    else
      msg += context[i];
  }

  /* The tabs before the error widen the displayed prefix, so the caret padding
     gains the extra columns each one renders beyond a single character. */
  usize tabs_before_error = 0;
  for (usize i = byte_position - start_offset; i < byte_position; i++)
    if (source[i] == '\t') tabs_before_error++;

  /* Calculate proper unicode offsets and lengths for underline. */
  const usize unicode_start_offset_position =
      SOURCE_LINE_INDEX.codepoints_before(source, byte_position - start_offset);

  /* Does token length go beyond that line? */
  const usize unicode_length = toiletline::utf8_strlen(
      source.data + byte_position, (byte_count > line_byte_count - start_offset)
                                       ? line_byte_count - start_offset
                                       : byte_count);

  /* Add spaces before the underline. */
  msg += '\n';
  msg += "       |  "; /* 10 chars */

  /* Starting amount of spaces before the error arrow beneath the context. */
  const usize added_symbols = 10;

  const usize underline_padding_length =
      (unicode_position - unicode_start_offset_position) +
      tabs_before_error * (TAB_WIDTH - 1) + added_symbols - 10;

  /* Remaining spaces to pad the underline. */
  for (usize i = 0; i < underline_padding_length; i++)
    msg += ' ';

  /* The underline itself of this token's length. The caret and its trailing
     message share clang's caret hue, so the span opens here and closes after
     the message. */
  msg += color.caret;
  msg += "^~";
  if (unicode_length > 2) {
    for (usize i = 0; i < unicode_length - 2; i++)
      msg += '~';
  }

  if (message.has_value()) {
    msg += ' ';
    msg += *message;
    msg += '.';
  }
  msg += color.reset;

  return msg;
}

ErrorBase::ErrorBase() = default;

ErrorBase::ErrorBase(StringView message) : m_is_active(true), m_message(message)
{}

ErrorBase::~ErrorBase() = default;

ErrorBase::operator bool &() throws { return m_is_active; }

cold fn ErrorBase::message() const throws -> String { return m_message; }

cold fn ErrorBase::severity_word() const wontthrow -> String { return "Error"; }

Error::Error(StringView message) : ErrorBase(message) {}

cold fn Error::to_string() const throws -> String
{
  let const color = diagnostic_colors_for(severity_word());
  return color.severity + severity_word() + color.reset + ": " +
         color.message + message() + "." + color.reset;
}

Error::operator String() const throws { return to_string(); }

Warning::Warning(StringView message) : Error(message) {}

cold fn Warning::severity_word() const wontthrow -> String { return "Warning"; }

Note::Note(StringView message) : Error(message) {}

cold fn Note::severity_word() const wontthrow -> String { return "Note"; }

ErrorWithLocation::ErrorWithLocation(SourceLocation location,
                                     StringView message)
    : ErrorBase(message), m_location(location)
{}

cold fn ErrorWithLocation::to_string(StringView source) const throws -> String
{
  usize byte_position = m_location.position;
  const usize byte_count = m_location.length;

  ASSERT(byte_position <= source.count(),
         "byte position: %zu, source length: %zu", byte_position,
         source.count());

  LOG_VARS(verbosity::Debug, byte_position, byte_count);
  LOG(verbosity::Debug, "formatting located %s", severity_word().c_str());

  /* FIXME: Below are two dirty hacks. */
  if (byte_position + 2 < source.count() && source[byte_position] == '\\' &&
      source[byte_position + 1] == '\n')
  {
    byte_position += 2;
  } else if (byte_position + 1 < source.count() &&
             source[byte_position] == '\n')
  {
    byte_position++;
  }

  auto [line_number, last_newline_location] =
      calc_precise_position(source, byte_position);

  const usize unicode_position =
      SOURCE_LINE_INDEX.codepoints_before(source, byte_position);

  /* Our count starts from 0. If there's only a single line, we need to use the
   * raw location for the correct offset. Otherwise, newline counts as an extra
   * character. */
  const usize line_byte_position =
      (last_newline_location > 0) ? unicode_position - last_newline_location
                                  : unicode_position + 1;

  let const color = diagnostic_colors_for(severity_word());

  String result{};
  /* A named source prefixes its path before the line and column, so a sourced
     error reads path:line:col rather than a bare line:col. */
  result += color.location;
  if (let const name = m_location.filename; name.has_value()) {
    result += *name;
    result += ':';
  }
  result += utils::uint_to_text(line_number + 1);
  result += ':';
  result += utils::uint_to_text(line_byte_position);
  result += ':';
  result += color.reset;
  result += ' ';
  result += color.severity;
  result += severity_word();
  result += color.reset;
  /* A located note with no message, such as a backtrace trace frame, ends at
     the severity word, so nothing follows the colon. */
  if (!m_message.is_empty()) {
    result += ": ";
    result += color.message;
    result += m_message;
    result += '.';
    result += color.reset;
  }
  result += '\n';

  result += get_context_pointing_to(source, byte_position, byte_count,
                                    line_number, last_newline_location,
                                    unicode_position, StringView{"here"}, color);
  return result;
}

WarningWithLocation::WarningWithLocation(SourceLocation location,
                                         StringView message)
    : ErrorWithLocation(location, message)
{}

cold fn WarningWithLocation::severity_word() const wontthrow -> String
{
  return "Warning";
}

TraceWithLocation::TraceWithLocation(SourceLocation location)
    : ErrorWithLocation(location, {})
{}

cold fn TraceWithLocation::severity_word() const wontthrow -> String
{
  return "Trace";
}

ErrorWithLocationAndDetails::ErrorWithLocationAndDetails(
    SourceLocation location, StringView message,
    SourceLocation details_location, StringView details_message)
    : ErrorWithLocation(location, message),
      m_details_location(details_location), m_details_message(details_message)
{}

cold fn ErrorWithLocationAndDetails::details_to_string(
    StringView source) const throws -> String
{
  usize byte_position = m_details_location.position;
  const usize byte_count = m_details_location.length;

  ASSERT(byte_position <= source.count(),
         "byte position: %zu, source length: %zu", byte_position,
         source.count());

  if (byte_position > 0 && byte_position == source.count() &&
      source[byte_position - 1] == '\n')
    byte_position--;

  auto [details_line_number, details_last_newline_location] =
      calc_precise_position(source, byte_position);

  const usize unicode_details_position =
      SOURCE_LINE_INDEX.codepoints_before(source, byte_position);

  const usize details_line_byte_position =
      (details_last_newline_location > 0)
          ? unicode_details_position - details_last_newline_location
          : unicode_details_position + 1;

  let const color = diagnostic_colors_for(StringView{"Note"});

  String result{};
  result += color.location;
  result += utils::uint_to_text(details_line_number + 1);
  result += ':';
  result += utils::uint_to_text(details_line_byte_position);
  result += ':';
  result += color.reset;
  result += ' ';
  result += color.severity;
  result += "Note";
  result += color.reset;
  result += ":\n";

  result += get_context_pointing_to(
      source, byte_position, byte_count, details_line_number,
      details_last_newline_location, unicode_details_position,
      m_details_message.view(), color);
  return result;
}

} /* namespace shit */
