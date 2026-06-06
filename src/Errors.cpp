#include "Errors.hpp"

#include "Debug.hpp"
#include "ErrorOr.hpp"
#include "Eval.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"

/* TODO: Print proper offset and context for UTF-8. */

namespace shit {

/* The line a byte falls on and the offset of the newline that starts it. */
struct precise_location
{
  usize line_number;
  usize last_newline_location;
};

cold static fn calc_precise_position(StringView source,
                                     usize byte_position) throws
    -> precise_location
{
  ASSERT(byte_position <= source.count(),
         "byte position: %zu, source length: %zu", byte_position,
         source.count());

  usize line_number = 0;
  usize last_newline_location = 0;

  for (usize i = 0; i < byte_position; i++) {
    if (source[i] != '\n') continue;
    last_newline_location = i;
    line_number++;
  }

  return precise_location{line_number, last_newline_location};
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
                                       Maybe<StringView> message) throws
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

  msg += std::to_string(line_number + 1) + " |  ";

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
      toiletline::utf8_strlen(source.data, byte_position - start_offset);

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

  /* The underline itself of this token's length. */
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

  return msg;
}

ErrorBase::ErrorBase() = default;

ErrorBase::ErrorBase(StringView message) : m_is_active(true), m_message(message)
{}

ErrorBase::~ErrorBase() = default;

ErrorBase::operator bool &() throws { return m_is_active; }

cold fn ErrorBase::message() const throws -> String { return m_message; }

cold fn ErrorBase::severity_word() const wontthrow -> String
{
  return "Error";
}

Error::Error(StringView message) : ErrorBase(message) {}

cold fn Error::to_string() const throws -> String
{
  return severity_word() + ": " + message() + ".";
}

Error::operator String() const throws { return to_string(); }

Warning::Warning(StringView message) : Error(message) {}

cold fn Warning::severity_word() const wontthrow -> String
{
  return "Warning";
}

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

  LOG_VARS(Verbosity::Debug, byte_position, byte_count);
  LOG(Verbosity::Debug, "formatting located %s", severity_word().c_str());

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
      toiletline::utf8_strlen(source.data, byte_position);

  /* Our count starts from 0. If there's only a single line, we need to use the
   * raw location for the correct offset. Otherwise, newline counts as an extra
   * character. */
  const usize line_byte_position =
      (last_newline_location > 0) ? unicode_position - last_newline_location
                                  : unicode_position + 1;

  String result{};
  /* A named source prefixes its path before the line and column, so a sourced
     error reads path:line:col rather than a bare line:col. */
  if (let const name = m_location.filename; name.has_value()) {
    result += *name;
    result += ':';
  }
  result += std::to_string(line_number + 1);
  result += ':';
  result += std::to_string(line_byte_position);
  result += ": ";
  result += severity_word();
  result += ": ";
  result += m_message;
  result += ".\n";

  result += get_context_pointing_to(source, byte_position, byte_count,
                                    line_number, last_newline_location,
                                    unicode_position, StringView{"here"});
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

TraceWithLocation::TraceWithLocation(SourceLocation location,
                                     StringView message)
    : ErrorWithLocation(location, message)
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
      toiletline::utf8_strlen(source.data, byte_position);

  const usize details_line_byte_position =
      (details_last_newline_location > 0)
          ? unicode_details_position - details_last_newline_location
          : unicode_details_position + 1;

  String result{};
  result += std::to_string(details_line_number + 1);
  result += ':';
  result += std::to_string(details_line_byte_position);
  result += ": Note:\n";

  result += get_context_pointing_to(
      source, byte_position, byte_count, details_line_number,
      details_last_newline_location, unicode_details_position,
      m_details_message.view());
  return result;
}

} /* namespace shit */
