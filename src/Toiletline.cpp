#include "Debug.hpp"
#include "Errors.hpp"

#include <vector>

namespace {

#define TL_ASSERT SHIT_ASSERT
#define TOILETLINE_IMPLEMENTATION
#include "toiletline/toiletline.h"

} /* namespace */

namespace toiletline {

bool
is_active()
{
  return itl_is_active;
}

void
initialize()
{
  if (::tl_init() != TL_SUCCESS)
    throw shit::Error{
        "Toiletline: Could not initialize. If you meant use stdin, "
        "provide '-' as an argument"};
}

void
exit()
{
  if (::tl_exit() != TL_SUCCESS)
    throw shit::Error{"Toiletline: Error while exiting"};
}

std::tuple<i32, std::string>
readline(usize max_buffer_size, std::string_view prompt)
{
  std::vector<char> b{};
  b.reserve(max_buffer_size);

  i32 code = ::tl_readline(b.data(), max_buffer_size, prompt.data());
  if (code == TL_ERROR)
    throw shit::Error{"Toiletline: Unexpected internal error"};

  return {code, b.data()};
}

void
enter_raw_mode()
{
  if (::tl_enter_raw_mode() != TL_SUCCESS)
    throw shit::Error{"Toiletline: Couldn't force the terminal into raw mode"};
}

void
exit_raw_mode()
{
  if (::tl_exit_raw_mode() != TL_SUCCESS)
    throw shit::Error{"Couldn't force the terminal to exit raw mode"};
}

void
emit_newlines(std::string_view buffer)
{
  if (tl_emit_newlines(buffer.data()) != TL_SUCCESS)
    throw shit::Error{"Toiletline: Couldn't emit newlines"};
}

} /* namespace toiletline */
