#include "Errors.hpp"

#include "Debug.hpp"
#include "Eval.hpp"
#include "Toiletline.hpp"

#include <optional>
#include <tuple>

/* TODO: Print proper offset and context for UTF-8. */

namespace shit {

static std::tuple<usize, usize>
calc_precise_position(std::string_view source, SourceLocation location)
{
  SHIT_ASSERT(location.position() <= source.length(),
              "location: %zu, length: %zu, source length: %zu",
              location.position(), location.length(), source.length());

  usize line_number = 0;
  usize last_newline_location = 0;

  for (usize i = 0; i < location.position(); i++) {
    if (source[i] == '\n') {
      last_newline_location = i;
      line_number++;
    }
  }

  return {line_number, last_newline_location};
}

static std::string
get_context_pointing_to(std::string_view source, SourceLocation location,
                        usize line_number, usize last_newline_location,
                        usize                           unicode_position,
                        std::optional<std::string_view> message)
{
  usize size = 0;
  usize start_offset = 0;

  usize position = location.position();

  while (position - start_offset > last_newline_location) {
    start_offset++;
  }

  while (position + size < source.length() && source[position + size] != '\n')
    size++;

  if (source[position - start_offset] == '\n') start_offset--;

  SHIT_ASSERT(position + size <= source.length(), "end: %zu, length: %zu",
              position + size, source.length());
  SHIT_ASSERT(position >= start_offset);
  SHIT_ASSERT(position - start_offset <= position + size,
              "location: %zu, start: %zu, size: %zu, ", position, start_offset,
              size);

  usize line_number_length = 0;
  usize line_number_copy = line_number + 1;

  while (line_number_copy > 0) {
    line_number_copy /= 10;
    line_number_length++;
  }

  std::string msg{};
  for (usize i = 0; i < 6 - line_number_length; i++) {
    msg += ' ';
  }

  msg += std::to_string(line_number + 1) + " |  ";

  std::string_view context =
      source.substr(position - start_offset, start_offset + size);

  /* We don't need accidental newlines in the middle of the context.
   * *pulls hair out* */
  SHIT_ASSERT(context.find('\n') == std::string::npos, "'%s'", context.data());

  msg += context;

  /* Calculate proper unicode offsets and lengths for underline. */
  usize unicode_start_offset_position =
      toiletline::utf8_strlen(source.data(), position - start_offset);
  usize unicode_length = toiletline::utf8_strlen(
      source.data() + location.position(), location.length());

  /* Add spaces before the underline. */
  msg += '\n';
  msg += "       |  "; /* 10 chars */

  /* Offset before the error arrow beneath the context. */
  usize added_symbols = 10;

  for (usize i = 0; i < (unicode_position - unicode_start_offset_position) +
                            added_symbols - 10;
       i++)
  {
    msg += ' ';
  }

  /* The underline itself of some length. */
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
  auto [line_number, last_newline_location] =
      calc_precise_position(source, m_location);

  usize unicode_position =
      toiletline::utf8_strlen(source.data(), m_location.position());

  /* Our count starts from 0. If there's only a single line, we need to use the
   * raw location for the correct offset. Otherwise, newline counts as an extra
   * character. */
  usize line_location = (last_newline_location > 0)
                            ? unicode_position - last_newline_location
                            : unicode_position + 1;
  return std::to_string(line_number + 1) + ":" + std::to_string(line_location) +
         ": Error: " + m_message + ".\n" +
         get_context_pointing_to(source, m_location, line_number,
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
  auto [details_line_number, details_last_newline_location] =
      calc_precise_position(source, m_details_location);

  usize unicode_details_position =
      toiletline::utf8_strlen(source.data(), m_details_location.position());

  usize details_line_location =
      (details_last_newline_location > 0)
          ? unicode_details_position - details_last_newline_location
          : unicode_details_position + 1;
  return std::to_string(details_line_number + 1) + ":" +
         std::to_string(details_line_location) + ": Note:" + "\n" +
         get_context_pointing_to(source, m_details_location,
                                 details_line_number,
                                 details_last_newline_location,
                                 unicode_details_position, m_details_message);
}

} /* namespace shit */
