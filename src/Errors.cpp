#include "Errors.hpp"

#include "Colors.hpp"
#include "Debug.hpp"
#include "ErrorOr.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

/* Each field is empty when color is off, so the render code appends them
   unconditionally and emits nothing on the plain path. */
struct diagnostic_color
{
  StringView severity{};
  StringView location{};
  StringView message{};
  StringView caret{};
  StringView reset{};
};

cold static fn diagnostic_colors_for(StringView severity_word) throws
    -> diagnostic_color
{
  if (!colors::stderr_wants_color()) return diagnostic_color{};

  let severity = colors::ansi::BOLD_CYAN;
  if (severity_word == StringView{"error"})
    severity = colors::ansi::BOLD_RED;
  else if (severity_word == StringView{"warning"})
    severity = colors::ansi::BOLD_MAGENTA;

  return diagnostic_color{severity, colors::ansi::BOLD, colors::ansi::BOLD,
                          colors::ansi::BOLD_GREEN, colors::ansi::RESET};
}

/* has_preceding_newline distinguishes a byte on the first line from one whose
   newline sits at offset zero, since both carry a zero last_newline_location.
 */
struct precise_location
{
  usize line_number;
  usize last_newline_location;
  bool has_preceding_newline;
};

/* A per-source index that turns the line and column lookup into a binary
   search, since otherwise N warnings cost N squared. */
class SourceLineIndex
{
public:
  SourceLineIndex()
      : m_newline_offsets(heap_allocator()),
        m_codepoints_before_newline(heap_allocator())
  {}

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
      if ((static_cast<u8>(source[i]) & 0xC0) != 0x80) codepoints++;
      if (source[i] == '\n') {
        m_newline_offsets.push(i);
        m_codepoints_before_newline.push(codepoints - 1);
      }
    }
  }

  /* A freed source can be replaced by a fresh allocation at the same address
     and length, which the pointer-and-length key cannot tell apart, so the
     next ensure_built_for must rebuild. */
  fn invalidate() wontthrow -> void
  {
    m_source_data = nullptr;
    m_source_length = 0;
    m_newline_offsets.clear();
    m_codepoints_before_newline.clear();
  }

  pure fn locate(usize byte_position) const wontthrow -> precise_location
  {
    const usize newlines_before = count_newlines_before(byte_position);
    const bool has_preceding_newline = newlines_before != 0;
    const usize last_newline_location =
        has_preceding_newline ? m_newline_offsets[newlines_before - 1] : 0;
    return precise_location{newlines_before, last_newline_location,
                            has_preceding_newline};
  }

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
  requires std::is_integral_v<T>
cold static fn number_string_length(T value) wontthrow -> usize
{
  usize digit_count = 1;
  while (value >= 10) {
    digit_count++;
    value /= 10;
  }
  return digit_count;
}

cold static fn
get_context_pointing_to(StringView source, usize byte_position,
                        usize byte_count, usize line_number,
                        usize last_newline_location, bool has_preceding_newline,
                        usize unicode_position, Maybe<StringView> message,
                        const diagnostic_color &color) throws -> String
{
  LOG(Debug, "assembling the caret context for line %zu", line_number + 1);

  usize start_offset = byte_position - last_newline_location;

  /* A preceding newline puts start_offset on that newline, so it steps one past
     to the first byte of the line. */
  if (has_preceding_newline && start_offset > 0) {
    start_offset--;
  }

  usize line_byte_count = 0;

  while (byte_position - start_offset + line_byte_count < source.count() &&
         source[byte_position - start_offset + line_byte_count] != '\n')
  {
    line_byte_count++;
  }

  ASSERT(byte_position - start_offset + line_byte_count == source.count() ||
         source[byte_position - start_offset + line_byte_count] == '\n');

  static constexpr usize LINE_NUMBER_FIELD_WIDTH = 6;
  static constexpr usize BAR_SEPARATOR_WIDTH = 4;
  const usize line_number_digit_count = number_string_length(line_number + 1);
  const usize line_number_padding_length =
      sub_sat(LINE_NUMBER_FIELD_WIDTH, line_number_digit_count);
  const usize gutter_width = line_number_padding_length +
                             line_number_digit_count + BAR_SEPARATOR_WIDTH;

  let msg = String{heap_allocator()};
  for (usize i = 0; i < line_number_padding_length; i++) {
    msg += ' ';
  }

  msg += String::from(line_number + 1, heap_allocator()) + " |  ";

  let const context =
      source.substring_of_length(byte_position - start_offset, line_byte_count);

  ASSERT(!context.find_character('\n').has_value(),
         "'%s', start: %zu, end: %zu", context.data, start_offset,
         line_byte_count);

  /* A tab renders wider than one byte, so it is expanded to a fixed width here
     and the caret padding below adds the same width per tab to stay aligned. */
  static constexpr usize TAB_WIDTH = 4;
  let display_line = String{heap_allocator()};
  for (usize i = 0; i < context.count(); i++) {
    if (context[i] == '\t')
      for (usize t = 0; t < TAB_WIDTH; t++)
        display_line += ' ';
    else
      display_line += context[i];
  }

  usize tabs_before_error = 0;
  for (usize i = byte_position - start_offset; i < byte_position; i++)
    if (source[i] == '\t') tabs_before_error++;

  const usize unicode_start_offset_position =
      SOURCE_LINE_INDEX.codepoints_before(source, byte_position - start_offset);

  /* The bounded counter walks at most the given byte count, so it never runs
     strlen past the source end at an EOF caret. */
  const usize caret_limit = sub_sat(line_byte_count, start_offset);
  const usize unicode_length = toiletline::utf8_strnlen(
      source.data + byte_position,
      byte_count > caret_limit ? caret_limit : byte_count);

  const usize caret_column =
      (unicode_position - unicode_start_offset_position) +
      tabs_before_error * (TAB_WIDTH - 1);

  const char *const line_bytes = display_line.view().data;
  const usize line_byte_length = display_line.count();
  const usize display_cells =
      toiletline::utf8_strnlen(line_bytes, line_byte_length);

  usize window_start = 0;
  usize window_end = display_cells;
  bool has_left_ellipsis = false;
  bool has_right_ellipsis = false;

  u32 terminal_columns = 0;
  u32 terminal_rows = 0;
  if (os::is_fd_a_tty(SHIT_STDERR) &&
      os::terminal_size(terminal_columns, terminal_rows) &&
      terminal_columns > gutter_width + 24 &&
      display_cells > terminal_columns - gutter_width)
  {
    const usize available = terminal_columns - gutter_width;
    const usize span = unicode_length < 1 ? 1 : unicode_length;
    const usize half = available / 2;
    const usize center = caret_column + span / 2;
    window_start = center > half ? center - half : 0;
    if (window_start + available > display_cells)
      window_start = display_cells - available;
    window_end = window_start + available;
    has_left_ellipsis = window_start > 0;
    has_right_ellipsis = window_end < display_cells;
    window_end = sub_sat(window_end, (has_left_ellipsis ? 3u : 0u) +
                                         (has_right_ellipsis ? 3u : 0u));
    if (window_start > caret_column) window_start = caret_column;
    if (caret_column + span > window_end && window_end < display_cells) {
      const usize shift = caret_column + span - window_end;
      window_start = window_start + shift > caret_column ? caret_column
                                                         : window_start + shift;
      window_end += shift;
    }
    if (window_end > display_cells) window_end = display_cells;
    if (window_end <= window_start) window_end = display_cells;
    has_left_ellipsis = window_start > 0;
    has_right_ellipsis = window_end < display_cells;
  }

  const usize window_start_byte = toiletline::byte_offset_of_codepoint(
      line_bytes, line_byte_length, window_start);
  const usize window_end_byte = toiletline::byte_offset_of_codepoint(
      line_bytes, line_byte_length, window_end);

  if (has_left_ellipsis) msg += "...";
  msg += display_line.view().substring_of_length(
      window_start_byte, window_end_byte - window_start_byte);
  if (has_right_ellipsis) msg += "...";

  const usize caret_pad =
      (has_left_ellipsis ? 3u : 0u) + sub_sat(caret_column, window_start);
  const usize caret_end = caret_column + unicode_length;
  const usize visible_caret =
      sub_sat(caret_end < window_end ? caret_end : window_end, caret_column);

  msg += '\n';
  for (usize i = 0; i + 3 < gutter_width; i++)
    msg += ' ';

  msg += "|  ";

  for (usize i = 0; i < caret_pad; i++)
    msg += ' ';

  msg += color.caret;
  msg += "^~";
  if (visible_caret > 2) {
    for (usize i = 0; i < visible_caret - 2; i++)
      msg += '~';
  }

  if (message.has_value()) {
    msg += ' ';
    msg += *message;

    let const view = *message;
    if (let const last_char =
            view.is_empty() ? '\0' : view.data[view.length - 1];
        last_char != '.' && last_char != '?' && last_char != '!')
      msg += '.';
  }
  msg += color.reset;

  return msg;
}

ErrorBase::ErrorBase() = default;

ErrorBase::ErrorBase(StringView message)
    : m_is_active(true), m_message(heap_allocator(), message)
{
  LOG(Debug, "constructing an error with message '%.*s'",
      static_cast<int>(message.length), message.data);
}

ErrorBase::~ErrorBase() = default;

ErrorBase::operator bool &() throws { return m_is_active; }

cold fn ErrorBase::message() const throws -> String { return m_message; }

cold fn ErrorBase::note_to_string() const throws -> String
{
  if (m_note.is_empty()) return String{heap_allocator()};

  let const color = diagnostic_colors_for(StringView{"note"});

  let const note_period =
      (m_note.back() == '.' || m_note.back() == '?' || m_note.back() == '!')
          ? ""
          : ".";

  return String{"\n"} + color.severity + "note" + color.reset + ": " +
         color.message + m_note + note_period + color.reset;
}

cold fn ErrorBase::severity_word() const wontthrow -> String { return "error"; }

Error::Error(StringView message) : ErrorBase(message) {}

cold fn ErrorBase::to_string(StringView source) const throws -> String
{
  unused(source);
  let const color = diagnostic_colors_for(severity_word());
  return color.severity + severity_word() + color.reset + ": " + color.message +
         message() + "." + color.reset + note_to_string();
}

fn Error::to_string() const throws -> String
{
  return ErrorBase::to_string(StringView{});
}

Error::operator String() const throws { return to_string(); }

Warning::Warning(StringView message) : Error(message) {}

InterruptError::InterruptError() : Error("Interrupted") {}

cold fn Warning::severity_word() const wontthrow -> String { return "warning"; }

Note::Note(StringView message) : Error(message) {}

cold fn Note::severity_word() const wontthrow -> String { return "note"; }

ExecFormatError::ExecFormatError()
    : Error("the file is not an executable and has no interpreter")
{}

ErrorWithDetails::ErrorWithDetails(StringView message, StringView note)
    : Error(message)
{
  m_note = note;
}

WarningWithDetails::WarningWithDetails(StringView message, StringView note)
    : Warning(message)
{
  m_note = note;
}

ErrorWithLocation::ErrorWithLocation(SourceLocation location,
                                     StringView message)
    : ErrorBase(message), m_location(steal(location))
{
  LOG(Debug, "locating the error at byte %zu spanning %zu bytes",
      m_location.position, m_location.length);
}

cold fn ErrorWithLocation::to_string(StringView source) const throws -> String
{
  usize byte_position = m_location.position;
  const usize byte_count = m_location.length;

  /* The location can name a byte in a source other than the one rendered, so
     the caret would read out of bounds and the message renders unlocated. */
  if (byte_position > source.count()) return ErrorBase::to_string(source);

  LOG_VARS(Debug, byte_position, byte_count);
  LOG(Debug, "formatting located %s", severity_word().c_str());

  /* A position on a line continuation or a bare newline is nudged past the
     backslash-newline pair or the lone newline to the next line. */
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

  /* Both terms must be code point counts, since subtracting a byte offset from
     a code point count underflows once a preceding line holds a multibyte byte.
     A newline at offset zero still starts line two, so the flag decides. */
  const usize codepoints_before_line =
      has_preceding_newline ? SOURCE_LINE_INDEX.codepoints_before(
                                  source, last_newline_location + 1)
                            : 0;
  const usize line_byte_position =
      unicode_position - codepoints_before_line + 1;

  let const color = diagnostic_colors_for(severity_word());

  let result = String{heap_allocator()};
  result += color.location;
  if (let const name = m_location.filename; name.has_value()) {
    result += *name;
    result += ':';
  }
  result += String::from(line_number + 1 + m_line_offset, heap_allocator());
  result += ':';
  result += String::from(line_byte_position, heap_allocator());
  result += ':';
  result += color.reset;
  result += ' ';
  result += color.severity;
  result += severity_word();
  result += color.reset;
  if (!m_message.is_empty()) {
    result += ": ";
    result += color.message;
    result += m_message;

    if (let const last_char = m_message.back();
        last_char != '.' && last_char != '?' && last_char != '!')
      result += '.';

    result += color.reset;
  } else {
    result += " location:";
  }
  result += '\n';

  result += get_context_pointing_to(
      source, byte_position, byte_count, line_number, last_newline_location,
      has_preceding_newline, unicode_position, StringView{"here"}, color);
  result += note_to_string();
  return result;
}

CommandNotFound::CommandNotFound(SourceLocation location, StringView message)
    : ErrorWithLocation(steal(location), message)
{}

CommandNotFound::CommandNotFound(SourceLocation location, StringView message,
                                 StringView note)
    : ErrorWithLocation(steal(location), message)
{
  m_note = note;
}

WarningWithLocation::WarningWithLocation(SourceLocation location,
                                         StringView message)
    : ErrorWithLocation(steal(location), message)
{}

cold fn WarningWithLocation::severity_word() const wontthrow -> String
{
  return "warning";
}

WarningWithLocationAndDetails::WarningWithLocationAndDetails(
    SourceLocation location, StringView message, StringView note)
    : WarningWithLocation(steal(location), message)
{
  m_note = note;
}

TraceWithLocation::TraceWithLocation(SourceLocation location)
    : ErrorWithLocation(steal(location), {})
{}

cold fn TraceWithLocation::severity_word() const wontthrow -> String
{
  return "trace";
}

ErrorWithLocationAndDetails::ErrorWithLocationAndDetails(
    SourceLocation location, StringView message,
    SourceLocation details_location, StringView details_message)
    : ErrorWithLocation(steal(location), message),
      m_details_location(steal(details_location)),
      m_details_message(details_message)
{}

ErrorWithLocationAndDetails::ErrorWithLocationAndDetails(
    SourceLocation location, StringView message, StringView note)
    : ErrorWithLocation(steal(location), message),
      m_details_message(heap_allocator())
{
  m_note = note;
}

cold fn ErrorWithLocationAndDetails::details_to_string(
    StringView source) const throws -> String
{
  if (m_details_message.is_empty()) return String{heap_allocator()};

  usize byte_position = m_details_location.position;
  const usize byte_count = m_details_location.length;

  /* The out-of-source guard renders nothing when the location names another
     source, so the caret never reads past the end. */
  if (byte_position > source.count()) return String{heap_allocator()};

  LOG(Debug, "formatting the detail note at byte %zu", byte_position);

  if (byte_position > 0 && byte_position == source.count() &&
      source[byte_position - 1] == '\n')
  {
    byte_position--;
  }

  auto [details_line_number, details_last_newline_location,
        details_has_preceding_newline] =
      calc_precise_position(source, byte_position);

  const usize unicode_details_position =
      SOURCE_LINE_INDEX.codepoints_before(source, byte_position);

  /* Both terms stay in code points. See ErrorWithLocation::to_string for why a
     byte offset here would underflow on a multibyte preceding line. */
  const usize codepoints_before_details_line =
      details_has_preceding_newline
          ? SOURCE_LINE_INDEX.codepoints_before(
                source, details_last_newline_location + 1)
          : 0;
  const usize details_line_byte_position =
      unicode_details_position - codepoints_before_details_line + 1;

  let const color = diagnostic_colors_for(StringView{"note"});

  let result = String{heap_allocator()};
  result += color.location;
  result += String::from(details_line_number + 1, heap_allocator());
  result += ':';
  result += String::from(details_line_byte_position, heap_allocator());
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

} // namespace shit
