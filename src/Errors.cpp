#include "Errors.hpp"

/**
 * class: ErrorBase
 */
ErrorBase::ErrorBase() : m_is_active(false) {}

ErrorBase::ErrorBase(std::string message)
    : m_is_active(true), m_message(message)
{
}

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
{
}

std::string
ErrorWithLocation::to_string(std::string_view source)
{
  calc_precise_position(source);
  m_message = std::to_string(m_line_number + 1) + ":" +
              std::to_string(m_location - m_last_newline_location + 1) + ": " +
              m_message + ".\n" + get_context(source);
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

  const usize offset_from_last_newline = m_location - m_last_newline_location;

  usize start_offset = 0;
  while (offset_from_last_newline - start_offset > 0 &&
         start_offset <= ERROR_CONTEXT_SIZE) {
    if (source[m_location - start_offset - 1] == '\n')
      break;
    start_offset++;
  }

  usize size = 0;
  while (offset_from_last_newline + size + 1 < source.length() &&
         source[m_last_newline_location + size + 1] != '\n' &&
         size <= ERROR_CONTEXT_SIZE)
    size++;

  INSIST(offset_from_last_newline + size <= source.length());
  INSIST(offset_from_last_newline - start_offset >= 0);
  INSIST(offset_from_last_newline - start_offset <=
         offset_from_last_newline + size);

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
  if (m_location - start_offset != m_last_newline_location) {
    msg += "..";
    added_symbols += 2;
  }
  msg += source.substr(m_location - start_offset, start_offset + size);
  /* did we cut the end? */
  if (size > ERROR_CONTEXT_SIZE)
    msg += "..";

  msg += "\n";
  for (usize i = 0; i < start_offset + added_symbols; i++)
    msg += ' ';
  msg += "^~ Error happened here.";

  return msg;
}
