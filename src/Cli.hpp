#pragma once

#include "Common.hpp"

#include <string>
#include <string_view>
#include <vector>

#define FLAG_LIST T__FLAG_LIST

#define FLAG_LIST_DECL()                                                       \
  static std::vector<shit::Flag *> FLAG_LIST {}

#define FLAG(var_name, kind, short_name, long_name, description)               \
  static shit::Flag##kind concat_literal(FLAG_, var_name){                     \
      short_name, long_name, description};                                     \
  static uchar concat_literal(t__flag_dummy_, __LINE__) =                      \
      (FLAG_LIST.emplace_back(&concat_literal(FLAG_, var_name)), 0)

namespace shit {

struct Flag
{
  enum class Kind : uint8_t
  {
    Bool,
    String,
    ManyStrings,
  };

  Kind             kind() const;
  u32              position() const;
  void             set_position(u32 n);
  char             short_name() const;
  std::string_view long_name() const;
  std::string_view description() const;

protected:
  Flag(Kind type, char short_name, const std::string &long_name,
       const std::string &description);

  Kind        m_kind;
  u32         m_position{0}; /* 0 if it wasn't specified. */
  char        m_short_name;
  std::string m_long_name;
  std::string m_description;
};

struct FlagBool : public Flag
{
  FlagBool(char short_name, const std::string &long_name,
           const std::string &description);

  void toggle();
  bool is_enabled() const;

private:
  bool m_value{};
};

struct FlagString : public Flag
{
  FlagString(char short_name, const std::string &long_name,
             const std::string &description);

  void             set(std::string_view v);
  bool             is_set() const;
  std::string_view value() const;

private:
  bool        m_is_set{false};
  std::string m_value{};
};

struct FlagManyStrings : public Flag
{
  FlagManyStrings(char short_name, const std::string &long_name,
                  const std::string &description);

  void             append(std::string_view v);
  bool             empty() const;
  usize            size() const;
  std::string_view get(usize i) const;

private:
  std::vector<std::string> m_values{};
};

/* These return arguments which are not flags. */

std::vector<std::string> parse_flags_vec(const std::vector<Flag *>      &flags,
                                         const std::vector<std::string> &args);
std::vector<std::string> parse_flags(const std::vector<Flag *> &flags, int argc,
                                     const char *const *argv);

void show_version();
void show_short_version();

void show_help(std::string_view program_name, const std::vector<Flag *> &flags);

void show_message(std::string_view err);

} /* namespace shit */
