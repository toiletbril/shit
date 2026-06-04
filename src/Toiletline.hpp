#include "Common.hpp"

#include <string>

namespace {

#include "toiletline/toiletline.h"

} /* namespace */

namespace toiletline {

void set_title(const std::string &title);

usize utf8_strlen(const std::string &s, usize byte_count = std::string::npos);

bool is_active();

void initialize();

void exit();

/* The outcome of one line read. The code is the toiletline status, such as
   enter, end of file, or an interrupt, and text is the bytes the user typed. */
struct InputResult
{
  i32 code;
  std::string text;
};

InputResult get_input(const std::string &prompt);

void set_input(const std::string &input);

void enter_raw_mode();

void exit_raw_mode();

void emit_newlines(std::string_view buffer);

} /* namespace toiletline */
