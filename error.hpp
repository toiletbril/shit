#pragma once

#include "types.hpp"

#include <iostream>
#include <string>
#include <tuple>

#define CONTEXT_SIZE 16

struct Error
{
  virtual std::string msg() = 0;
  virtual ~Error() {}

protected:
  std::tuple<usize, usize>
  precise_position(usize location, std::string_view source)
  {
    usize line_number           = 0;
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
  get_context(usize location, usize line_location, std::string_view source)
  {
    usize start_offset = 0;
    usize size         = 0;

    while (line_location - start_offset > 0 && start_offset <= CONTEXT_SIZE)
      start_offset++;
    while (line_location + size < source.length() &&
           source[line_location + size] != '\n' && size <= CONTEXT_SIZE)
      size++;

    std::string msg;
    msg += source.substr(location - start_offset, size).data();
    msg += "\n";
    for (usize i = 0; i < start_offset; i++)
      msg += ' ';
    msg += "^ Error happened here.";
    return msg;
  }
};

struct ParserError : public Error
{
  ParserError(usize location, std::string_view source, std::string message)
      : m_message(message)
  {
    auto [line, last_newline] = precise_position(location, source);

    m_message = std::to_string(line) + ":" +
                std::to_string(location - last_newline) + ": " + m_message +
                ".\n" + get_context(location, location - last_newline, source);
  }

  std::string
  msg()
  {
    return m_message;
  }

private:
  std::string m_message;
};
