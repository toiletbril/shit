#include "Errors.hpp"

#include "Colors.hpp"
#include "Debug.hpp"
#include "ErrorOr.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* TODO: Print proper offset and context for UTF-8. */

namespace shit {

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

/* The color codes for a diagnostic of this severity, or all empty StringViews
   when color is off. The severity word selects the severity hue, since the
   reporting code reads the word from the object rather than its concrete type.
 */
cold static fn diagnostic_colors_for(StringView severity_word) throws
    -> diagnostic_color
{
  if (!colors::stderr_wants_color()) return diagnostic_color{};

  StringView severity = colors::ansi::BOLD_CYAN;
  if (severity_word == StringView{"error"})
    severity = colors::ansi::BOLD_RED;
  else if (severity_word == StringView{"warning"})
    severity = colors::ansi::BOLD_MAGENTA;

  return diagnostic_color{severity, colors::ansi::BOLD, colors::ansi::BOLD,
                          colors::ansi::BOLD_GREEN, colors::ansi::RESET};
}

/* The line a byte falls on and the offset of the newline that starts it. The
   has_preceding_newline flag tells the two apart from a byte on the first line,
   since a newline at offset zero starts line two yet shares the zero offset with
   the no-newline case. */
struct precise_location
{
  usize line_number;
  usize last_newline_location;
  bool has_preceding_newline;
};

/* A per-source index that turns the line and column lookup from a scan over the
   whole prefix into a binary search. Without it a warning deep in the file
   costs time proportional to its byte offset, so N warnings cost N squared. The
   shell reuses one source for a whole analysis pass, so a single cached entry
   keyed on the source pointer and length serves every located message in that
   pass. */
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

  /* Forget the cached source, so the next ensure_built_for rebuilds rather than
     trusting the pointer and length. A retained source freed by the host can be
     replaced by a fresh allocation at the same address with the same length,
     which the pointer-and-length key alone cannot tell apart. */
  fn invalidate() wontthrow -> void
  {
    m_source_data = nullptr;
    m_source_length = 0;
    m_newline_offsets.clear();
    m_codepoints_before_newline.clear();
  }

  /* The line number and the offset of the newline that starts it, matching the
     prefix scan it replaces. The line number counts newlines strictly before
     the byte, and the newline offset is the last such newline, or zero when the
     byte is on the first line. */
  pure fn locate(usize byte_position) const wontthrow -> precise_location
  {
    const usize newlines_before = count_newlines_before(byte_position);
    const bool has_preceding_newline = newlines_before != 0;
    const usize last_newline_location =
        has_preceding_newline ? m_newline_offsets[newlines_before - 1] : 0;
    return precise_location{newlines_before, last_newline_location,
                            has_preceding_newline};
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

fn invalidate_source_line_index() wontthrow -> void
{
  SOURCE_LINE_INDEX.invalidate();
}

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

cold static fn get_context_pointing_to(
    StringView source, usize byte_position, usize byte_count, usize line_number,
    usize last_newline_location, bool has_preceding_newline,
    usize unicode_position, Maybe<StringView> message,
    const diagnostic_color &color) throws -> String
{
  usize start_offset = byte_position - last_newline_location;

  /* A preceding newline puts start_offset on that newline, so it steps one past
     to the first byte of the line. A newline at offset zero starts line two and
     must step too, which the flag captures where the bare offset zero could not.
   */
  if (has_preceding_newline && start_offset > 0) start_offset--;

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

  /* Does token length go beyond that line? The bounded counter walks at most
     the given byte count over the interior pointer, so it never runs strlen
     past the source end at an EOF caret. */
  const usize unicode_length = toiletline::utf8_strnlen(
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

cold fn ErrorBase::severity_word() const wontthrow -> String { return "error"; }

Error::Error(StringView message) : ErrorBase(message) {}

cold fn Error::to_string() const throws -> String
{
  let const color = diagnostic_colors_for(severity_word());
  return color.severity + severity_word() + color.reset + ": " + color.message +
         message() + "." + color.reset;
}

Error::operator String() const throws { return to_string(); }

Warning::Warning(StringView message) : Error(message) {}

cold fn Warning::severity_word() const wontthrow -> String { return "warning"; }

Note::Note(StringView message) : Error(message) {}

cold fn Note::severity_word() const wontthrow -> String { return "note"; }

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

  auto [line_number, last_newline_location, has_preceding_newline] =
      calc_precise_position(source, byte_position);

  const usize unicode_position =
      SOURCE_LINE_INDEX.codepoints_before(source, byte_position);

  /* The column counts code points from the line start to the caret. Both terms
     must be code point counts, since subtracting the newline's byte offset from
     a code point count underflows once a preceding line holds a multibyte byte.
     On the first line there is no newline, so the count from the source start
     plus one gives the column. A newline at offset zero still starts line two,
     so the flag rather than the bare offset decides whether a line precedes. */
  const usize codepoints_before_line =
      has_preceding_newline ? SOURCE_LINE_INDEX.codepoints_before(
                                  source, last_newline_location + 1)
                            : 0;
  const usize line_byte_position =
      unicode_position - codepoints_before_line + 1;

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

  result += get_context_pointing_to(
      source, byte_position, byte_count, line_number, last_newline_location,
      has_preceding_newline, unicode_position, StringView{"here"}, color);
  return result;
}

CommandNotFound::CommandNotFound(SourceLocation location, StringView message)
    : ErrorWithLocation(location, message)
{}

WarningWithLocation::WarningWithLocation(SourceLocation location,
                                         StringView message)
    : ErrorWithLocation(location, message)
{}

cold fn WarningWithLocation::severity_word() const wontthrow -> String
{
  return "warning";
}

TraceWithLocation::TraceWithLocation(SourceLocation location)
    : ErrorWithLocation(location, {})
{}

cold fn TraceWithLocation::severity_word() const wontthrow -> String
{
  return "trace";
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

  auto [details_line_number, details_last_newline_location,
        details_has_preceding_newline] =
      calc_precise_position(source, byte_position);

  const usize unicode_details_position =
      SOURCE_LINE_INDEX.codepoints_before(source, byte_position);

  /* The column counts code points from the line start to the caret, so both
     terms stay in code points. See ErrorWithLocation::to_string for why a byte
     offset here would underflow on a multibyte preceding line, and why a newline
     at offset zero needs the flag rather than the bare offset. */
  const usize codepoints_before_details_line =
      details_has_preceding_newline
          ? SOURCE_LINE_INDEX.codepoints_before(
                source, details_last_newline_location + 1)
          : 0;
  const usize details_line_byte_position =
      unicode_details_position - codepoints_before_details_line + 1;

  let const color = diagnostic_colors_for(StringView{"note"});

  String result{};
  result += color.location;
  result += utils::uint_to_text(details_line_number + 1);
  result += ':';
  result += utils::uint_to_text(details_line_byte_position);
  result += ':';
  result += color.reset;
  result += ' ';
  result += color.severity;
  result += "note";
  result += color.reset;
  result += ":\n";

  result += get_context_pointing_to(
      source, byte_position, byte_count, details_line_number,
      details_last_newline_location, details_has_preceding_newline,
      unicode_details_position, m_details_message.view(), color);
  return result;
}

} /* namespace shit */
