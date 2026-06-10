#pragma once

#include "Common.hpp"
#include "Containers.hpp"

#define FLAG_LIST T__FLAG_LIST

#define HELP_SYNOPSIS T__FLAG_HELP_SYNOPSIS

#define HELP_SYNOPSIS_DECL(...)                                                \
  static shit::ArrayList<shit::StringView> HELP_SYNOPSIS { __VA_ARGS__ }

#define HELP_DESCRIPTION T__FLAG_HELP_DESCRIPTION

#define HELP_DESCRIPTION_DECL(text)                                            \
  static shit::StringView HELP_DESCRIPTION { text }

#define FLAG_LIST_DECL()                                                       \
  static shit::ArrayList<shit::Flag *> FLAG_LIST { shit::heap_allocator() }

#define FLAG(var_name, kind, short_name, long_name, description)               \
  static shit::Flag##kind concat_literal(FLAG_, var_name){                     \
      short_name, long_name, description};                                     \
  static uchar concat_literal(t__flag_dummy_, __LINE__) =                      \
      (FLAG_LIST.push(&concat_literal(FLAG_, var_name)), 0)

namespace shit {

extern const usize HELP_WRAP_WIDTH;
extern const usize HELP_INDENT;

class Flag
{
public:
  enum class Kind : u8
  {
    Bool,
    String,
    ManyStrings,
  };

  pure fn kind() const wontthrow -> Kind;
  pure fn position() const wontthrow -> usize;
  fn set_position(u32 n) throws -> void;
  pure fn short_name() const wontthrow -> char;
  pure fn long_name() const wontthrow -> StringView;
  pure fn description() const wontthrow -> StringView;

  /* Reset the flag state, mainly for builtins. */
  virtual fn reset() throws -> void = 0;

protected:
  Flag(Kind type, char short_name, StringView long_name,
       StringView description);

  Kind m_kind;
  usize m_position{0}; /* 0 if it wasn't specified. */
  char m_short_name;
  String m_long_name;
  String m_description;
};

class FlagBool : public Flag
{
public:
  FlagBool(char short_name, StringView long_name, StringView description);

  fn toggle() throws -> void;
  pure fn is_enabled() const wontthrow -> bool;

  fn reset() throws -> void override;

private:
  bool m_value{false};
};

class FlagString : public Flag
{
public:
  FlagString(char short_name, StringView long_name, StringView description);

  fn set(StringView v) throws -> void;
  pure fn is_set() const wontthrow -> bool;
  pure fn value() const wontthrow -> StringView;

  fn reset() throws -> void override;

private:
  bool m_is_set{false};
  String m_value{};
};

class FlagManyStrings : public Flag
{
public:
  FlagManyStrings(char short_name, StringView long_name,
                  StringView description);

  fn append(StringView v) throws -> void;
  pure fn count() const wontthrow -> usize;
  pure fn is_empty() const wontthrow -> bool;

  pure fn get(usize i) const wontthrow -> StringView;

  fn next() throws -> StringView;
  pure fn at_end() const wontthrow -> bool;

  fn reset() throws -> void override;

private:
  ArrayList<String> m_values{};
  usize m_value_position{0};
};

/* These return arguments which are not flags. */

fn parse_flags_vec(const ArrayList<Flag *> &flags,
                   const ArrayList<String> &args,
                   usize base_position = 0) throws -> ArrayList<String>;
fn parse_flags(const ArrayList<Flag *> &flags, int argc, const char *const *argv,
               usize base_position = 0) throws -> ArrayList<String>;

/* Join the arguments into one space-separated line, the source a located flag
   error renders its caret against. The caller rebuilds the same line the parser
   measured its offsets in, so the caret lands under the offending flag. */
fn join_command_line(int argc, const char *const *argv) throws -> String;

fn reset_flags(const ArrayList<Flag *> &flags) throws -> void;

fn show_version() throws -> void;
fn show_short_version() throws -> void;

fn make_synopsis(StringView program_name,
                 const ArrayList<StringView> &lines) throws -> String;
fn make_flag_help(const ArrayList<Flag *> &flags) throws -> String;

/* Word-wrap text so no line exceeds width columns, with every line indented by
   indent spaces. The text breaks only at a space, so a word is never split, and
   a single word longer than the available room is emitted whole. The result has
   no trailing newline. */
fn wrap_text(StringView text, usize indent, usize width) throws -> String;

fn show_message(StringView err) throws -> void;

/* Write bytes to the standard streams without going through the iostream
   layer. The shell uses these instead of std::cout and std::cerr so the binary
   does not pull in the stream machinery. */
fn print(StringView text) throws -> void;
fn print_error(StringView text) throws -> void;
fn flush() throws -> void;

} /* namespace shit */
