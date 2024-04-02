#include "Errors.hpp"

ErrorBase::ErrorBase() : m_is_active(false) {}

ErrorBase::ErrorBase(usize location, std::string message)
    : m_is_active(true), m_message(message), m_location(location)
{
}

ErrorBase::~ErrorBase() = default;

ErrorBase::operator bool &() { return m_is_active; }

void
ErrorBase::calc_precise_position(std::string_view source)
{
  INSIST(m_is_active);
  INSIST(m_location <= source.length());

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
ErrorBase::get_context(std::string_view source)
{
  INSIST(m_is_active);

  const usize offset_from_last_newline = m_location - m_last_newline_location;

  usize size = 0;
  usize start_offset = 0;
  while (offset_from_last_newline - start_offset > 0 &&
         start_offset <= ERROR_CONTEXT_SIZE)
    start_offset++;
  while (offset_from_last_newline + size < source.length() &&
         source[offset_from_last_newline + size] != '\n' &&
         size <= ERROR_CONTEXT_SIZE)
    size++;

  INSIST(offset_from_last_newline + size <= source.length());
  INSIST(offset_from_last_newline - start_offset >= 0);
  INSIST(offset_from_last_newline - start_offset <
         offset_from_last_newline + size);

  std::string msg;
  msg += source.substr(offset_from_last_newline - start_offset,
                       start_offset + size);
  msg += "\n";
  for (usize i = 0; i < start_offset; i++)
    msg += ' ';
  msg += "^~ Error happened here.";

  return msg;
}

Error::Error() : ErrorBase() {}

Error::Error(usize location, std::string message) : ErrorBase(location, message)
{
}

std::string
Error::to_string(std::string_view source)
{
  calc_precise_position(source);
  m_message = std::to_string(m_line_number) + ":" +
              std::to_string(m_location - m_last_newline_location) + ": " +
              m_message + ".\n" + get_context(source);
  return m_message;
}
