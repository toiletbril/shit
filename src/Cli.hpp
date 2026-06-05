#pragma once

#include "Common.hpp"
#include "Containers.hpp"

#include <string>
#include <string_view>
#include <vector>

#define FLAG_LIST T__FLAG_LIST

#define HELP_SYNOPSIS T__FLAG_HELP_SYNOPSIS

#define HELP_SYNOPSIS_DECL(...)                                                \
  static std::vector<std::string> HELP_SYNOPSIS { __VA_ARGS__ }

#define FLAG_LIST_DECL()                                                       \
  static shit::ArrayList<shit::Flag *> FLAG_LIST { shit::heap_allocator() }

#define FLAG(var_name, kind, short_name, long_name, description)               \
  static shit::Flag##kind concat_literal(FLAG_, var_name){                     \
      short_name, long_name, description};                                     \
  static uchar concat_literal(t__flag_dummy_, __LINE__) =                      \
      (FLAG_LIST.push(&concat_literal(FLAG_, var_name)), 0)

namespace shit {

struct Flag
{
  enum class Kind : u8
  {
    Bool,
    String,
    ManyStrings,
  };

  Kind kind() const;
  usize position() const;
  void set_position(u32 n);
  char short_name() const;
  StringView long_name() const;
  StringView description() const;

  /* Reset the flag state, mainly for builtins. */
  virtual void reset() = 0;

protected:
  Flag(Kind type, char short_name, StringView long_name,
       StringView description);

  Kind m_kind;
  usize m_position{0}; /* 0 if it wasn't specified. */
  char m_short_name;
  String m_long_name;
  String m_description;
};

struct FlagBool : public Flag
{
  FlagBool(char short_name, StringView long_name,
           StringView description);

  void toggle();
  bool is_enabled() const;

  void reset() override;

private:
  bool m_value{false};
};

struct FlagString : public Flag
{
  FlagString(char short_name, StringView long_name,
             StringView description);

  void set(StringView v);
  bool is_set() const;
  StringView value() const;

  void reset() override;

private:
  bool m_is_set{false};
  String m_value{};
};

struct FlagManyStrings : public Flag
{
  FlagManyStrings(char short_name, StringView long_name,
                  StringView description);

  void append(StringView v);
  usize size() const;
  bool is_empty() const;

  StringView get(usize i) const;

  StringView next();
  bool at_end() const;

  void reset() override;

private:
  ArrayList<String> m_values{};
  usize m_value_position{0};
};

/* These return arguments which are not flags. */

ArrayList<String> parse_flags_vec(const ArrayList<Flag *> &flags,
                                         const ArrayList<String> &args);
ArrayList<String> parse_flags(const ArrayList<Flag *> &flags, int argc,
                                     const char *const *argv);

void reset_flags(const ArrayList<Flag *> &flags);

void show_version();
void show_short_version();

std::string make_synopsis(std::string_view program_name,
                          const std::vector<std::string> &lines);
std::string make_flag_help(const ArrayList<Flag *> &flags);

void show_message(StringView err);

/* Write bytes to the standard streams without going through the iostream
   layer. The shell uses these instead of std::cout and std::cerr so the binary
   does not pull in the stream machinery. */
void print_to_standard_output(StringView text);
void print_to_standard_error(StringView text);
void flush_standard_output();

} /* namespace shit */
