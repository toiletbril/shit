#pragma once

/* A runtime-gated logger, separate from the compile-time TRACE in
   Debug.hpp. TRACE compiles out in a release build, while LOG stays
   in and checks the level against a global, so a helper can trace the runtime
   in any build when the user raises the verbosity. */

#include "Common.hpp"
#include "Containers.hpp"

namespace shit {

enum class verbosity : u8
{
  Nothing = 0,
  Info,
  Debug,
  All,
};

/* The active log level. A message prints when its level is at or below this.
   Main sets it from the -X flag, so a release build that leaves the Nothing
   default pays one comparison per call and prints nothing. */
inline verbosity LOGGER_VERBOSITY = verbosity::Nothing;

/* The log sink, stderr unless --debug-output-file pointed it at a file opened
   for append, so an interactive session logs without painting over the
   prompt. Main owns the lifetime, the file stays open for the whole run. */
inline std::FILE *LOGGER_OUTPUT = nullptr;

inline std::FILE *log_output_stream()
{
  return LOGGER_OUTPUT != nullptr ? LOGGER_OUTPUT : stderr;
}

constexpr const char *verbosity_to_string(verbosity verbosity)
{
  switch (verbosity) {
  case verbosity::Nothing: return "OFF";
  case verbosity::Info: return "INF";
  case verbosity::Debug: return "DBG";
  case verbosity::All: return "ALL";
  }
  return "???";
}

namespace log_detail {

inline String value_to_log_string(StringView value) { return String{value}; }

inline String value_to_log_string(const char *value)
{
  return value != nullptr ? String{value} : String{"(null)"};
}

inline String value_to_log_string(bool value)
{
  return value ? String{"true"} : String{"false"};
}

inline String value_to_log_string(char value)
{
  String out{};
  out.push(value);
  return out;
}

template <class T>
  requires std::is_integral_v<T>
String value_to_log_string(T value)
{
  char buffer[32];
  if constexpr (std::is_signed_v<T>) {
    std::snprintf(buffer, sizeof(buffer), "%lld",
                  static_cast<long long>(value));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%llu",
                  static_cast<unsigned long long>(value));
  }
  return String{buffer};
}

template <class T>
  requires std::is_floating_point_v<T>
String value_to_log_string(T value)
{
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
  return String{buffer};
}

template <class T>
  requires std::is_pointer_v<T>
String value_to_log_string(T value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%p", static_cast<const void *>(value));
  return String{buffer};
}

/* Pair each comma-separated name in names with its value and build
   "a = 1, b = 2". The names come from the stringized argument list, so the
   output reads like the source that called it. */
template <class... Args>
String format_named_values(StringView names, Args &&...args)
{
  String out{};
  usize index = 0;
  const usize count = sizeof...(Args);

  auto append_one = [&](auto &&value) {
    StringView name = names;
    Maybe<usize> comma_position = names.find_character(',');
    if (comma_position.has_value()) {
      name = names.substring_of_length(0, comma_position.value());
      names = names.substring(comma_position.value() + 1);
    } else {
      names = StringView{};
    }
    usize start = 0;
    while (start < name.length &&
           (name.data[start] == ' ' || name.data[start] == '\t'))
      start++;
    usize stop = name.length;
    while (stop > start &&
           (name.data[stop - 1] == ' ' || name.data[stop - 1] == '\t'))
      stop--;
    name = name.substring_of_length(start, stop - start);

    out.append(name);
    out.append(StringView{" = "});
    out.append(value_to_log_string(value).view());
    if (++index < count) out.append(StringView{", "});
  };

  (append_one(std::forward<Args>(args)), ...);
  return out;
}

} /* namespace log_detail */

} /* namespace shit */

#define T__LOG_STRINGIZE2(x) #x
#define T__LOG_STRINGIZE(x)  T__LOG_STRINGIZE2(x)

/* A release build carries no logging at all, neither the format strings nor
   the call sites, so the binary stays lean and the hot paths lose even the
   level comparison. The -X flag disappears with it, a debug build is the one
   that traces. */
#if defined NDEBUG

#define LOG(level, ...)                                                        \
  do {                                                                         \
  } while (0)
#define LOG_VARS(level, ...)                                                   \
  do {                                                                         \
  } while (0)

#else /* NDEBUG */

/* Print a printf-style message at the given level when the level is active.
   The file:line and the function render right-aligned in fixed columns so a
   tailed log reads as a table. The flush after each message keeps a tailed
   --debug-logging-file current while the TUI is still alive. */
#define LOG(level, ...)                                                        \
  do {                                                                         \
    if ((level) <= ::shit::LOGGER_VERBOSITY) [[unlikely]] {                    \
      std::FILE *t__log_stream = ::shit::log_output_stream();                  \
      unused(std::fprintf(t__log_stream, "[%s] %32s %32s(): ",                 \
                          ::shit::verbosity_to_string(level),                  \
                          __FILE__ ":" T__LOG_STRINGIZE(__LINE__), __func__)); \
      unused(std::fprintf(t__log_stream, __VA_ARGS__));                        \
      unused(std::fputc('\n', t__log_stream));                                 \
      unused(std::fflush(t__log_stream));                                      \
    }                                                                          \
  } while (0)

/* Print the named values of the listed variables, such as
   LOG_VARS(verbosity::Debug, argument_count, name). */
#define LOG_VARS(level, ...)                                                   \
  do {                                                                         \
    if ((level) <= ::shit::LOGGER_VERBOSITY) [[unlikely]] {                    \
      ::shit::String t__vars =                                                 \
          ::shit::log_detail::format_named_values(#__VA_ARGS__, __VA_ARGS__);  \
      std::FILE *t__log_stream = ::shit::log_output_stream();                  \
      unused(std::fprintf(t__log_stream, "[%s] %32s %32s(): %s\n",             \
                          ::shit::verbosity_to_string(level),                  \
                          __FILE__ ":" T__LOG_STRINGIZE(__LINE__), __func__,   \
                          t__vars.c_str()));                                   \
      unused(std::fflush(t__log_stream));                                      \
    }                                                                          \
  } while (0)

#endif /* !NDEBUG */
