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
        "Could not initialize toiletline. If you meant use stdin, "
        "provide '-' as an argument"};
}

void
exit()
{
  if (::tl_exit() != TL_SUCCESS)
    throw shit::Error{"Error while exiting toiletline"};
}

std::tuple<i32, std::string>
readline(usize max_buffer_size, std::string_view prompt)
{
  std::vector<char> b{};
  b.reserve(max_buffer_size);

  i32 code = ::tl_readline(b.data(), max_buffer_size, prompt.data());
  if (code == TL_ERROR)
    throw shit::Error{"Unexpected internal toiletline error"};

  return {code, b.data()};
}

void
enter_raw_mode()
{
  if (!::itl_enter_raw_mode())
    throw shit::Error{"Couldn't force the terminal into raw mode"};
}

void
exit_raw_mode()
{
  if (!::itl_exit_raw_mode())
    throw shit::Error{"Couldn't force the terminal to exit raw mode"};
}

} // namespace toiletline
