/* Toiletline.hpp is not included to define toiletline configuration macros here
 * rather than in the header. */

#include "Debug.hpp"
#include "Errors.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

#include <tuple>
#include <vector>

namespace {

#define TL_NO_SUSPEND
#define TL_ASSERT SHIT_ASSERT

#define TOILETLINE_IMPLEMENTATION
#include "toiletline/toiletline.h"

} /* namespace */

namespace toiletline {

static const std::string SHIT_HISTORY_FILE = ".shit_history";

bool
is_active()
{
  return ::itl_global_is_active;
}

void
initialize()
{
  /* Load history. */
  if (std::optional<std::filesystem::path> h = shit::os::get_home_directory();
      h.has_value())
  {
    std::filesystem::path shit_history = *h / SHIT_HISTORY_FILE;
    if (int e = ::tl_history_load(shit_history.string().c_str());
        e != TL_SUCCESS)
    {
      /* Don't count non-existent history file as an error. */
      if (e != ENOENT) {
        throw shit::Error{"Toiletline: Could not load history: " +
                          shit::os::last_system_error_message()};
      }
    }
  }

  if (::tl_init() != TL_SUCCESS) {
    throw shit::Error{
        "Toiletline: Could not initialize. If you meant use stdin, "
        "provide '-' as an argument"};
  }
}

void
exit()
{
  /* Dump history. */
  if (std::optional<std::filesystem::path> h = shit::os::get_home_directory();
      h.has_value())
  {
    std::filesystem::path shit_history = *h / SHIT_HISTORY_FILE;
    if (::tl_history_dump(shit_history.string().c_str()) != TL_SUCCESS) {
      throw shit::Error{"Toiletline: Could not dump history: " +
                        shit::os::last_system_error_message()};
    }
  }

  if (::tl_exit() != TL_SUCCESS) {
    throw shit::Error{"Toiletline: Error while exiting"};
  }
}

std::tuple<i32, std::string>
readline(usize max_buffer_size, std::string_view prompt)
{
  std::vector<char> b{};
  b.reserve(max_buffer_size);

  i32 code = ::tl_readline(b.data(), max_buffer_size, prompt.data());
  if (code == TL_ERROR) {
    throw shit::Error{"Toiletline: Unexpected internal error"};
  }

  return {code, b.data()};
}

void
enter_raw_mode()
{
  if (::tl_enter_raw_mode() != TL_SUCCESS) {
    throw shit::Error{"Toiletline: Couldn't force the terminal into raw mode"};
  }
}

void
exit_raw_mode()
{
  if (::tl_exit_raw_mode() != TL_SUCCESS) {
    throw shit::Error{"Couldn't force the terminal to exit raw mode"};
  }
}

void
emit_newlines(std::string_view buffer)
{
  if (::tl_emit_newlines(buffer.data()) != TL_SUCCESS) {
    throw shit::Error{"Toiletline: Couldn't emit newlines"};
  }
}

} /* namespace toiletline */
