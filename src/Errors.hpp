#pragma once

#include "Common.hpp"
#include "Debug.hpp"

#include <iostream>
#include <string>
#include <tuple>

#define ERROR_CONTEXT_SIZE 24

struct ErrorBase
{
  ErrorBase();

  ErrorBase(usize location, std::string message);

  virtual ~ErrorBase();

  operator bool &();

  virtual std::string to_string(std::string_view source) = 0;

protected:
  bool        m_is_active{false};
  std::string m_message;

  usize m_location{0};
  usize m_line_number{0};
  usize m_last_newline_location{0};

  void calc_precise_position(std::string_view source);

  std::string get_context(std::string_view source);
};

struct Error : public ErrorBase
{
  Error();

  Error(usize location, std::string message);

  std::string to_string(std::string_view source);
};
