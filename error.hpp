#pragma once

#include "types.hpp"

#include <string>
#include <tuple>

struct Error
{
  virtual std::string msg() = 0;

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
};

struct LexerError : public Error
{
  LexerError(usize location, std::string_view source, std::string message)
      : m_message(message)
  {
    auto [line, last_newline] = precise_position(location, source);

    m_message = std::to_string(line) + ":" +
                std::to_string(location - last_newline) + ": " +
                "Lexer error: " + m_message;
  }

  std::string
  msg()
  {
    return m_message;
  }

private:
  std::string m_message;
};

struct ParserError : public Error
{
  ParserError(usize location, std::string_view source, std::string message)
      : m_message(message)
  {
    auto [line, last_newline] = precise_position(location, source);

    m_message = std::to_string(line) + ":" +
                std::to_string(location - last_newline) + ": " +
                "Parser error: " + m_message;
  }

  std::string
  msg()
  {
    return m_message;
  }

private:
  std::string m_message;
};
