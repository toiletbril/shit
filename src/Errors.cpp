#include "Errors.hpp"

#include "Debug.hpp"

#include <tuple>

namespace shit {

static std::tuple<usize, usize>
calc_precise_position(std::string_view source, usize location)
{
  SHIT_ASSERT(location <= source.length(), "location: %zu, length: %zu",
              location, source.length());

  usize line_number = 0;
  usize last_newline_location = 0;

  for (usize i = 0; i < location; i++) {
    if (source[i] == '\n') {
      last_newline_location = i;
      line_number++;
    }
  }

  return {line_number, last_newline_location};
}

std::string
get_context_pointing_to(std::string_view source, usize location,
                        usize line_number, usize last_newline_location,
                        std::string_view message)
{
  usize start_offset = 0;
  while (location - start_offset > last_newline_location &&
         start_offset <= ERROR_CONTEXT_SIZE)
  {
    start_offset++;
  }

  usize size = 0;
  while (location + size < source.length() && source[location + size] != '\n' &&
         size <= ERROR_CONTEXT_SIZE)
  {
    size++;
  }

  if (source[location - start_offset] == '\n') {
    start_offset--;
  }

  SHIT_ASSERT(location + size <= source.length(), "end: %zu, length: %zu",
              location + size, source.length());
  SHIT_ASSERT(location >= start_offset);
  SHIT_ASSERT(location - start_offset <= location + size,
              "location: %zu, start: %zu, size: %zu, ", location, start_offset,
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

  /* Offset before the error arrow beneath the context. */
  usize added_symbols = 10;

  msg += std::to_string(line_number + 1) + " |  ";

  /* Did we cut the start? */
  if (location - start_offset != last_newline_location + 1 &&
      location - start_offset != 0)
  {
    msg += "..";
    added_symbols += 2;
  }

  std::string_view context =
      source.substr(location - start_offset, start_offset + size);

  /* We don't need accidental newlines in the middle of the context.
   * *pulls hair out* */
  SHIT_ASSERT(context.find('\n') == std::string::npos, "'%s'", context.data());

  msg += context;

  /* Did we cut the end? */
  if (size > ERROR_CONTEXT_SIZE) {
    msg += "..";
  }

  msg += "\n";
  for (usize i = 0; i < start_offset + added_symbols; i++) {
    msg += ' ';
  }
  msg += "^~ ";
  msg += message;
  msg += '.';

  return msg;
}

/**
 * class: ErrorBase
 */
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

/**
 * class: Error
 */
Error::Error() : ErrorBase() {}

Error::Error(const std::string &message) : ErrorBase(message) {}

std::string
Error::to_string()
{
  return "Error: " + message() + ".";
}

/**
 * class: ErrorLocation
 */
ErrorWithLocation::ErrorWithLocation() : ErrorBase() {}

ErrorWithLocation::ErrorWithLocation(usize location, const std::string &message)
    : ErrorBase(message), m_location(location)
{}

std::string
ErrorWithLocation::to_string(std::string_view source)
{
  auto [line_number, last_newline_location] =
      calc_precise_position(source, m_location);

  /* Our count starts from 0. If there's only a single line, we need to use the
   * raw location for the correct offset. Otherwise, newline counts as an extra
   * character. */
  usize line_location = (last_newline_location > 0)
                            ? m_location - last_newline_location
                            : m_location + 1;
  m_message =
      std::to_string(line_number + 1) + ":" + std::to_string(line_location) +
      ": Error: " + m_message + ".\n" +
      get_context_pointing_to(source, m_location, line_number,
                              last_newline_location, "Error happened here");
  return m_message;
}

/**
 * class: ErrorLocation
 */
ErrorWithLocationAndDetails::ErrorWithLocationAndDetails() : ErrorWithLocation()
{}

ErrorWithLocationAndDetails::ErrorWithLocationAndDetails(
    usize location, const std::string &message, usize details_location,
    const std::string &details_message)
    : ErrorWithLocation(location, message),
      m_details_location(details_location), m_details_message(details_message)
{}

std::string
ErrorWithLocationAndDetails::details_to_string(std::string_view source)
{
  auto [details_line_number, details_last_newline_location] =
      calc_precise_position(source, m_details_location);

  usize details_line_location =
      (details_last_newline_location > 0)
          ? m_details_location - details_last_newline_location
          : m_details_location + 1;
  m_message =
      std::to_string(details_line_number + 1) + ":" +
      std::to_string(details_line_location) + ": Note:" + "\n" +
      get_context_pointing_to(source, m_details_location, details_line_number,
                              details_last_newline_location, m_details_message);
  return m_message;
}

} /* namespace shit */
