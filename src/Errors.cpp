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

cold static fn get_context_pointing_to(
    StringView source, usize byte_position, usize byte_count,
    const utils::source_line_position &line_position, Maybe<StringView> message,
    const diagnostic_color &color) throws -> String
{
  const usize line_number = line_position.line_number;
  LOG(Debug, "assembling the caret context for line %zu", line_number + 1);

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

  const usize line_byte_count =
      line_position.line_end - line_position.line_start;
  let const context =
      source.substring_of_length(line_position.line_start, line_byte_count);
  const usize caret_byte_position = byte_position - line_position.line_start;

  ASSERT(!context.find_character('\n').has_value(),
         "'%s', start: %zu, end: %zu", context.data, line_position.line_start,
         line_position.line_end);

  static constexpr usize TAB_WIDTH = 4;
  usize tab_count = 0;
  usize tabs_before_error = 0;
  for (usize i = 0; i < context.count(); i++) {
    if (context[i] != '\t') continue;
    tab_count++;
    if (i < caret_byte_position) tabs_before_error++;
  }

  let expanded_line = String{heap_allocator()};
  let display_line = context;
  if (tab_count != 0) {
    expanded_line.reserve(context.count() + tab_count * (TAB_WIDTH - 1));
    for (usize i = 0; i < context.count(); i++) {
      if (context[i] == '\t') {
        for (usize tab_column = 0; tab_column < TAB_WIDTH; tab_column++)
          expanded_line += ' ';
      } else {
        expanded_line += context[i];
      }
    }
    display_line = expanded_line.view();
  }

  const usize caret_limit = line_position.line_end - byte_position;
  const usize unicode_length = toiletline::utf8_strnlen(
      source.data + byte_position,
      byte_count > caret_limit ? caret_limit : byte_count);

  const usize caret_column =
      toiletline::utf8_strnlen(context.data, caret_byte_position) +
      tabs_before_error * (TAB_WIDTH - 1);

  const char *const line_bytes = display_line.data;
  const usize line_byte_length = display_line.count();
  const usize display_cells =
      toiletline::utf8_strnlen(line_bytes, line_byte_length);

  usize window_start = 0;
  usize window_end = display_cells;
  bool has_left_ellipsis = false;
  bool has_right_ellipsis = false;

  u32 terminal_columns = 0;
  u32 terminal_rows = 0;
  if (display_cells > 24 && os::is_fd_a_tty(SHIT_STDERR) &&
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
  msg += display_line.substring_of_length(window_start_byte,
                                          window_end_byte - window_start_byte);
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

ErrorBase::ErrorBase(StringView message) : m_message(heap_allocator(), message)
{
  LOG(Debug, "constructing an error with message '%.*s'",
      static_cast<int>(message.length), message.data);
}

ErrorBase::~ErrorBase() = default;

cold fn ErrorBase::trailing_details_to_string() const throws -> String
{
  let const note = detail_message();
  if (note.is_empty()) return String{heap_allocator()};

  let const color = diagnostic_colors_for(StringView{"note"});

  let const final_byte = note[note.length - 1];
  let const note_period =
      (final_byte == '.' || final_byte == '?' || final_byte == '!') ? "" : ".";

  return String{"\n"} + color.severity + "note" + color.reset + ": " +
         color.message + note + note_period + color.reset;
}

cold fn ErrorBase::severity_word() const wontthrow -> StringView
{
  return "error";
}

Error::Error(StringView message) : ErrorBase(message) {}

ErrorWithDetails::ErrorWithDetails(StringView message, StringView note)
    : Error(message), m_note(note)
{}

cold fn ErrorBase::to_string(StringView source) const throws -> String
{
  unused(source);
  let const severity = severity_word();
  let const color = diagnostic_colors_for(severity);
  return color.severity + severity + color.reset + ": " + color.message +
         message() + "." + color.reset + trailing_details_to_string();
}

fn Error::to_string() const throws -> String
{
  return ErrorBase::to_string(StringView{});
}

Warning::Warning(StringView message) : Error(message) {}

WarningWithDetails::WarningWithDetails(StringView message, StringView note)
    : Warning(message), m_note(note)
{}

InterruptErrorWithLocation::InterruptErrorWithLocation(SourceLocation location)
    : ErrorWithLocation(location, "Interrupted")
{}

cold fn Warning::severity_word() const wontthrow -> StringView
{
  return "warning";
}

Note::Note(StringView message) : Error(message) {}

cold fn Note::severity_word() const wontthrow -> StringView { return "note"; }

BrokenPipeExit::BrokenPipeExit() : Error("broken pipe") {}

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
  if (source.data == nullptr || byte_position > source.count())
    return ErrorBase::to_string(source);

  LOG_VARS(Debug, byte_position, byte_count);
  let const severity = severity_word();
  LOG(Debug, "formatting located %.*s", static_cast<int>(severity.length),
      severity.data);

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

  let const line_position =
      utils::source_line_position_at(source, byte_position);
  const usize line_byte_position =
      toiletline::utf8_strnlen(source.data + line_position.line_start,
                               byte_position - line_position.line_start) +
      1;
  let const color = diagnostic_colors_for(severity);

  let result = String{heap_allocator()};
  result += color.location;
  if (let const name = m_location.filename; name.has_value()) {
    result += *name;
    result += ':';
  }
  result += String::from(line_position.line_number + 1 + m_line_offset,
                         heap_allocator());
  result += ':';
  result += String::from(line_byte_position, heap_allocator());
  result += ':';
  result += color.reset;
  result += ' ';
  result += color.severity;
  result += severity;
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

  result += get_context_pointing_to(source, byte_position, byte_count,
                                    line_position, StringView{"here"}, color);
  result += trailing_details_to_string();
  return result;
}

CommandResolutionErrorWithLocation::CommandResolutionErrorWithLocation(
    SourceLocation location, StringView message, i64 command_status)
    : ErrorWithLocation(steal(location), message)
{
  set_command_status(command_status);
}

CommandResolutionErrorWithLocationAndDetails::
    CommandResolutionErrorWithLocationAndDetails(SourceLocation location,
                                                 StringView message,
                                                 StringView note,
                                                 i64 command_status)
    : CommandResolutionErrorWithLocation(steal(location), message,
                                         command_status),
      m_note(note)
{}

WarningWithLocation::WarningWithLocation(SourceLocation location,
                                         StringView message)
    : ErrorWithLocation(steal(location), message)
{}

WarningWithLocationAndDetails::WarningWithLocationAndDetails(
    SourceLocation location, StringView message, StringView note)
    : WarningWithLocation(steal(location), message), m_note(note)
{}

cold fn WarningWithLocation::severity_word() const wontthrow -> StringView
{
  return "warning";
}

TraceWithLocation::TraceWithLocation(SourceLocation location)
    : ErrorWithLocation(steal(location), {})
{}

cold fn TraceWithLocation::severity_word() const wontthrow -> StringView
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
      m_details_message(heap_allocator()), m_note(note)
{}

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

  let const details_line_position =
      utils::source_line_position_at(source, byte_position);
  const usize details_line_byte_position =
      toiletline::utf8_strnlen(source.data + details_line_position.line_start,
                               byte_position -
                                   details_line_position.line_start) +
      1;

  let const color = diagnostic_colors_for(StringView{"note"});

  let result = String{heap_allocator()};
  result += color.location;
  if (let const name = m_details_location.filename; name.has_value()) {
    result += *name;
    result += ':';
  }
  result +=
      String::from(details_line_position.line_number + 1, heap_allocator());
  result += ':';
  result += String::from(details_line_byte_position, heap_allocator());
  result += ':';
  result += color.reset;
  result += ' ';
  result += color.severity;
  result += "note";
  result += color.reset;
  result += ":\n";

  result += get_context_pointing_to(source, byte_position, byte_count,
                                    details_line_position,
                                    m_details_message.view(), color);
  return result;
}

} // namespace shit
