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

  fn kind() const -> Kind;
  fn position() const -> usize;
  fn set_position(u32 n) -> void;
  fn short_name() const -> char;
  fn long_name() const -> StringView;
  fn description() const -> StringView;

  /* Reset the flag state, mainly for builtins. */
  virtual fn reset() -> void = 0;

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
  FlagBool(char short_name, StringView long_name, StringView description);

  fn toggle() -> void;
  fn is_enabled() const -> bool;

  fn reset() -> void override;

private:
  bool m_value{false};
};

struct FlagString : public Flag
{
  FlagString(char short_name, StringView long_name, StringView description);

  fn set(StringView v) -> void;
  fn is_set() const -> bool;
  fn value() const -> StringView;

  fn reset() -> void override;

private:
  bool m_is_set{false};
  String m_value{};
};

struct FlagManyStrings : public Flag
{
  FlagManyStrings(char short_name, StringView long_name,
                  StringView description);

  fn append(StringView v) -> void;
  fn size() const -> usize;
  fn is_empty() const -> bool;

  fn get(usize i) const -> StringView;

  fn next() -> StringView;
  fn at_end() const -> bool;

  fn reset() -> void override;

private:
  ArrayList<String> m_values{};
  usize m_value_position{0};
};

/* These return arguments which are not flags. */

fn parse_flags_vec(const ArrayList<Flag *> &flags,
                   const ArrayList<String> &args) -> ArrayList<String>;
fn parse_flags(const ArrayList<Flag *> &flags, int argc,
               const char *const *argv) -> ArrayList<String>;

fn reset_flags(const ArrayList<Flag *> &flags) -> void;

fn show_version() -> void;
fn show_short_version() -> void;

fn make_synopsis(std::string_view program_name,
                 const std::vector<std::string> &lines) -> std::string;
fn make_flag_help(const ArrayList<Flag *> &flags) -> std::string;

fn show_message(StringView err) -> void;

/* Write bytes to the standard streams without going through the iostream
   layer. The shell uses these instead of std::cout and std::cerr so the binary
   does not pull in the stream machinery. */
fn print(StringView text) -> void;
fn print_error(StringView text) -> void;
fn flush() -> void;

} /* namespace shit */
