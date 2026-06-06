#include "Common.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace {

#include "toiletline/toiletline.h"

} /* namespace */

namespace toiletline {

using shit::String;
using shit::StringView;

void set_title(const String &title);

usize utf8_strlen(const String &s, usize byte_count = static_cast<usize>(-1));

bool is_active();

void initialize();

void exit();

/* The outcome of one line read. The code is the toiletline status, such as
   enter, end of file, or an interrupt, and text is the bytes the user typed. */
struct input_result
{
  i32 code;
  String text;
};

input_result get_input(const String &prompt);

void set_input(const String &input);

void enter_raw_mode();

void exit_raw_mode();

void emit_newlines(StringView buffer);

} /* namespace toiletline */
