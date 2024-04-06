#include "Errors.hpp"

/**
 * class: ErrorBase
 */
ErrorBase::ErrorBase() : m_is_active(false) {}

ErrorBase::ErrorBase(std::string message)
    : m_is_active(true), m_message(message)
{}

ErrorBase::~ErrorBase() = default;

ErrorBase::operator bool &() { return m_is_active; }

/**
 * class: Error
 */

Error::Error() : ErrorBase() {}

Error::Error(std::string message) : ErrorBase(message) {}

std::string
Error::to_string()
{
  return m_message;
}

/**
 * class: ErrorLocation
 */
ErrorWithLocation::ErrorWithLocation() : ErrorBase() {}

ErrorWithLocation::ErrorWithLocation(usize location, std::string message)
    : ErrorBase(message), m_location(location)
{}

std::string
ErrorWithLocation::to_string(std::string_view source)
{
  calc_precise_position(source);
  /* Our count starts from 0. If there's only a single line, we need to use the
   * raw location for the correct offset. Otherwise, newline counts as an extra
   * character. */
  usize line_location = (m_last_newline_location > 0)
                            ? m_location - m_last_newline_location
                            : m_location + 1;
  m_message = std::to_string(m_line_number + 1) + ":" +
              std::to_string(line_location) + ": " + m_message + ".\n" +
              get_context(source);
  return m_message;
}

void
ErrorWithLocation::calc_precise_position(std::string_view source)
{
  INSIST(m_is_active);
  INSIST(m_location <= source.length(), "location: %zu, length: %zu",
         m_location, source.length());

  /* Already called. */
  if (m_line_number != 0)
    return;

  usize line_number = 0;
  usize last_newline_location = 0;

  for (usize i = 0; i < m_location; i++) {
    if (source[i] == '\n') {
      last_newline_location = i;
      line_number++;
    }
  }

  m_line_number = line_number;
  m_last_newline_location = last_newline_location;
}

std::string
ErrorWithLocation::get_context(std::string_view source) const
{
  INSIST(m_is_active);

  usize start_offset = 0;
  while (m_location - start_offset > m_last_newline_location &&
         start_offset <= ERROR_CONTEXT_SIZE)
  {
    start_offset++;
  }

  usize size = 0;
  while (m_location + size < source.length() &&
         source[m_location + size] != '\n' && size <= ERROR_CONTEXT_SIZE)
  {
    size++;
  }

  if (source[m_location - start_offset] == '\n')
    start_offset--;

  INSIST(m_location + size <= source.length(), "end: %zu, length: %zu",
         m_location + size, source.length());
  INSIST(m_location - start_offset >= 0);
  INSIST(m_location - start_offset <= m_location + size,
         "location: %zu, start: %zu, size: %zu, ", m_location, start_offset,
         size);

  usize line_number_length = 0;
  usize line_number_copy = m_line_number + 1;
  while (line_number_copy > 0) {
    line_number_copy /= 10;
    line_number_length++;
  }

  std::string msg;
  for (usize i = 0; i < 6 - line_number_length; i++)
    msg += ' ';

  /* offset before the error arrow beneath the context. */
  usize added_symbols = 10;

  msg += std::to_string(m_line_number + 1) + " |  ";

  /* did we cut the start? */
  if (m_location - start_offset != m_last_newline_location + 1 &&
      m_location - start_offset != 0)
  {
    msg += "..";
    added_symbols += 2;
  }

  std::string_view context =
      source.substr(m_location - start_offset, start_offset + size);
  /* we don't need accidental newlines in the middle of the context.
   * *pulls hair out* */
  INSIST(context.find('\n') == std::string::npos, "'%s'", context.data());
  msg += context;

  /* did we cut the end? */
  if (size > ERROR_CONTEXT_SIZE)
    msg += "..";

  msg += "\n";
  for (usize i = 0; i < start_offset + added_symbols; i++)
    msg += ' ';
  msg += "^~ Error happened here.";

  return msg;
}
