#include "Errors.hpp"

#include "Debug.hpp"
#include "Eval.hpp"
#include "Toiletline.hpp"

#include <optional>
#include <tuple>

/* TODO: Print proper offset and context for UTF-8. */

namespace shit {

static std::tuple<usize, usize>
calc_precise_position(std::string_view source, usize byte_position)
{
  SHIT_ASSERT(byte_position <= source.length(),
              "byte position: %zu, source length: %zu", byte_position,
              source.length());

  usize line_number = 0, last_newline_location = 0;

  for (usize i = 0; i < byte_position; i++) {
    if (source[i] != '\n') continue;
    last_newline_location = i;
    line_number++;
  }

  return {line_number, last_newline_location};
}

template <class T>
static usize
number_string_length(T n)
{
  usize len = 0;
  while (n > 0) {
    len++;
    n /= 10;
  }
  return len;
}

static std::string
get_context_pointing_to(std::string_view source, usize byte_position,
                        usize byte_count, usize line_number,
                        usize last_newline_location, usize unicode_position,
                        std::optional<std::string_view> message)
{
  /* Offset from the start of the line. */
  usize start_offset = byte_position - last_newline_location;

  /* If we have a newline before, start_offset points to this newline. Get rid
   * of it. */
  if (last_newline_location != 0 && start_offset > 0) start_offset--;

  /* Find out where the next newline is. */
  usize line_byte_count = 0;

  while (byte_position - start_offset + line_byte_count < source.length() &&
         source[byte_position - start_offset + line_byte_count] != '\n')
  {
    line_byte_count++;
  }

  SHIT_ASSERT(source[byte_position - start_offset + line_byte_count] == '\n' ||
              byte_position - start_offset + line_byte_count ==
                  source.length());

  /* Add spacer before line number. */
  std::string msg{};
  for (usize i = 0; i < 6 - number_string_length(line_number + 1); i++) {
    msg += ' ';
  }

  msg += std::to_string(line_number + 1) + " |  ";

  /* Line that caused the error. */
  std::string_view context =
      source.substr(byte_position - start_offset, line_byte_count);

  /* We don't need accidental newlines in the middle of the context.
   * *pulls hair out* */
  SHIT_ASSERT(context.find('\n') == std::string::npos,
              "'%s', start: %zu, end: %zu", context.data(), start_offset,
              line_byte_count);

  msg += context;

  /* Calculate proper unicode offsets and lengths for underline. */
  usize unicode_start_offset_position =
      toiletline::utf8_strlen(source.data(), byte_position - start_offset);

  /* Does token length go beyond that line? */
  usize unicode_length =
      toiletline::utf8_strlen(source.data() + byte_position,
                              (byte_count > line_byte_count - start_offset)
                                  ? line_byte_count - start_offset
                                  : byte_count);

  /* Add spaces before the underline. */
  msg += '\n';
  msg += "       |  "; /* 10 chars */

  /* Starting amount of spaces before the error arrow beneath the context. */
  usize added_symbols = 10;

  usize underline_padding_length =
      (unicode_position - unicode_start_offset_position) + added_symbols - 10;

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

ErrorBase::ErrorBase(const std::string &message)
    : m_is_active(true), m_message(message)
{}

ErrorBase::~ErrorBase() = default;

ErrorBase::operator bool &() { return m_is_active; }

std::string
ErrorBase::message() const
{
  return m_message;
}

Error::Error(const std::string &message) : ErrorBase(message) {}

std::string
Error::to_string() const
{
  return "Error: " + message() + ".";
}

ErrorWithLocation::ErrorWithLocation(SourceLocation     location,
                                     const std::string &message)
    : ErrorBase(message), m_location(location)
{}

std::string
ErrorWithLocation::to_string(std::string_view source) const
{
  usize byte_position = m_location.position();
  usize byte_count = m_location.length();

  /* FIXME: Below are two dirty hacks. */
  if (byte_position + 2 < source.length() && source[byte_position] == '\\' &&
      source[byte_position + 1] == '\n')
  {
    byte_position += 2;
  } else if (byte_position + 1 < source.length() &&
             source[byte_position] == '\n')
  {
    byte_position++;
  }

  auto [line_number, last_newline_location] =
      calc_precise_position(source, byte_position);

  usize unicode_position =
      toiletline::utf8_strlen(source.data(), byte_position);

  /* Our count starts from 0. If there's only a single line, we need to use the
   * raw location for the correct offset. Otherwise, newline counts as an extra
   * character. */
  usize line_byte_position = (last_newline_location > 0)
                                 ? unicode_position - last_newline_location
                                 : unicode_position + 1;

  return std::to_string(line_number + 1) + ":" +
         std::to_string(line_byte_position) + ": Error: " + m_message + ".\n" +
         get_context_pointing_to(source, byte_position, byte_count, line_number,
                                 last_newline_location, unicode_position,
                                 "here");
}

ErrorWithLocationAndDetails::ErrorWithLocationAndDetails(
    SourceLocation location, const std::string &message,
    SourceLocation details_location, const std::string &details_message)
    : ErrorWithLocation(location, message),
      m_details_location(details_location), m_details_message(details_message)
{}

std::string
ErrorWithLocationAndDetails::details_to_string(std::string_view source) const
{
  usize byte_position = m_details_location.position();
  usize byte_count = m_details_location.length();

  if (byte_position == source.length() && source[byte_position - 1] == '\n')
    byte_position--;

  auto [details_line_number, details_last_newline_location] =
      calc_precise_position(source, byte_position);

  usize unicode_details_position =
      toiletline::utf8_strlen(source.data(), byte_position);

  usize details_line_byte_position =
      (details_last_newline_location > 0)
          ? unicode_details_position - details_last_newline_location
          : unicode_details_position + 1;

  return std::to_string(details_line_number + 1) + ":" +
         std::to_string(details_line_byte_position) + ": Note:" + "\n" +
         get_context_pointing_to(source, byte_position, byte_count,
                                 details_line_number,
                                 details_last_newline_location,
                                 unicode_details_position, m_details_message);
}

} /* namespace shit */
