#pragma once

/* A runtime-gated logger, separate from the compile-time SHIT_TRACE in
   Debug.hpp. SHIT_TRACE compiles out in a release build, while SHIT_LOG stays
   in and checks the level against a global, so a helper can trace the runtime
   in any build when the user raises the verbosity. */

#include "Common.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

namespace shit {

enum class Verbosity : u8
{
  Nothing = 0,
  Error,
  Warn,
  Info,
  Debug,
  All,
};

/* The active log level. A message prints when its level is at or below this.
   Main sets it from a flag, so a release build that leaves the default pays one
   comparison per call. */
inline Verbosity g_log_verbosity = Verbosity::Warn;

constexpr const char *
verbosity_to_string(Verbosity verbosity)
{
  switch (verbosity) {
  case Verbosity::Nothing: return "Nothing";
  case Verbosity::Error: return "Error";
  case Verbosity::Warn: return "Warn";
  case Verbosity::Info: return "Info";
  case Verbosity::Debug: return "Debug";
  case Verbosity::All: return "All";
  }
  return "?";
}

namespace log_detail {

inline std::string
value_to_log_string(std::string_view value)
{
  return std::string{value};
}

inline std::string
value_to_log_string(const std::string &value)
{
  return value;
}

inline std::string
value_to_log_string(const char *value)
{
  return value != NULL ? std::string{value} : std::string{"(null)"};
}

inline std::string
value_to_log_string(bool value)
{
  return value ? "true" : "false";
}

inline std::string
value_to_log_string(char value)
{
  return std::string{value};
}

template <class T>
  requires std::is_integral_v<T>
std::string
value_to_log_string(T value)
{
  return std::to_string(value);
}

template <class T>
  requires std::is_floating_point_v<T>
std::string
value_to_log_string(T value)
{
  return std::to_string(value);
}

template <class T>
  requires std::is_pointer_v<T>
std::string
value_to_log_string(T value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%p",
                static_cast<const void *>(value));
  return std::string{buffer};
}

/* Pair each comma-separated name in names with its value and build
   "a = 1, b = 2". The names come from the stringized argument list, so the
   output reads like the source that called it. */
template <class... Args>
std::string
format_named_values(std::string_view names, Args &&...args)
{
  std::string out{};
  std::size_t index = 0;
  const std::size_t count = sizeof...(Args);

  auto append_one = [&](auto &&value) {
    std::string_view name = names;
    const std::size_t comma_position = names.find(',');
    if (comma_position != std::string_view::npos) {
      name = names.substr(0, comma_position);
      names = names.substr(comma_position + 1);
    } else {
      names = std::string_view{};
    }
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
      name.remove_prefix(1);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
      name.remove_suffix(1);

    out += std::string{name};
    out += " = ";
    out += value_to_log_string(value);
    if (++index < count) out += ", ";
  };

  (append_one(std::forward<Args>(args)), ...);
  return out;
}

} /* namespace log_detail */

} /* namespace shit */

/* Print a printf-style message at the given level when the level is active. */
#define SHIT_LOG(level, ...)                                                   \
  do {                                                                         \
    if ((level) <= ::shit::g_log_verbosity) [[unlikely]] {                     \
      SHIT_UNUSED(std::fprintf(stderr, "[%s] " __FILE__ ":%d %s(): ",          \
                               ::shit::verbosity_to_string(level), __LINE__,   \
                               __func__));                                     \
      SHIT_UNUSED(std::fprintf(stderr, __VA_ARGS__));                          \
      SHIT_UNUSED(std::fputc('\n', stderr));                                   \
    }                                                                          \
  } while (0)

/* Print the named values of the listed variables, such as
   SHIT_LOG_VARS(Verbosity::Debug, argument_count, name). */
#define SHIT_LOG_VARS(level, ...)                                              \
  do {                                                                         \
    if ((level) <= ::shit::g_log_verbosity) [[unlikely]] {                     \
      std::string t__vars =                                                    \
          ::shit::log_detail::format_named_values(#__VA_ARGS__, __VA_ARGS__);  \
      SHIT_UNUSED(std::fprintf(stderr, "[%s] " __FILE__ ":%d %s(): %s\n",      \
                               ::shit::verbosity_to_string(level), __LINE__,   \
                               __func__, t__vars.c_str()));                    \
    }                                                                          \
  } while (0)
