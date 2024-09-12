/* Toiletline.hpp is not included to define toiletline configuration macros here
 * rather than in the header. */

#include "Cli.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

#include <tuple>
#include <vector>

/* TODO: Unexpected internal error :shrug: */
namespace {

#define TL_NO_SUSPEND
#define TL_ASSERT           SHIT_ASSERT
#define TL_HISTORY_MAX_SIZE 1024

#define TOILETLINE_IMPLEMENTATION
#include "toiletline/toiletline.h"

} /* namespace */

namespace toiletline {

static const std::string SHIT_HISTORY_FILE = ".shit_history";

bool
set_title(const std::string &title)
{
  return ::tl_set_title(title.c_str()) != -1;
}

usize
utf8_strlen(const std::string &s)
{
  return ::tl_utf8_strlen(s.c_str());
}

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
    if (int ret = ::tl_history_load(shit_history.string().c_str());
        ret != TL_SUCCESS)
    {
      /* Don't count non-existent history file as an error. */
      if (ret != -ENOENT) {
        std::string err_message = "Toiletline: Could not load history: ";
        err_message += (errno == EINVAL)
                           ? std::string{"Non-text byte detected in history "
                                         "file. Truncate it manually"}
                           : shit::os::last_system_error_message();
        shit::Error e{err_message};
        shit::show_message(e.to_string());
      }
    }
  }

  if (::tl_init() != TL_SUCCESS) {
    throw shit::Error{"Toiletline: Could not initialize the terminal. If you "
                      "meant use stdin, provide '-' as an argument"};
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
    if (int ret = ::tl_history_dump(shit_history.string().c_str());
        ret != TL_SUCCESS && ret != -EINVAL)
    {
      shit::Error e{"Toiletline: Could not dump history: " +
                    shit::os::last_system_error_message()};
      shit::show_message(e.to_string());
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
    throw shit::Error{
        "Toiletline: Couldn't force the terminal to exit raw mode"};
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
