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
  };

  Flag(Kind type, char short_name, const std::string &long_name,
       const std::string &description);

  Kind             kind() const;
  char             short_name() const;
  std::string_view long_name() const;
  std::string_view description() const;

protected:
  Kind        m_kind;
  char        m_short_name;
  std::string m_long_name;
  std::string m_description;
};

struct FlagBool : public Flag
{
  FlagBool(char short_name, const std::string &long_name,
           const std::string &description);

  void toggle();
  bool enabled() const;

private:
  bool m_value{};
};

struct FlagString : public Flag
{
  FlagString(char short_name, const std::string &long_name,
             const std::string &description);

  void             set(std::string_view v);
  std::string_view contents() const;

private:
  std::string m_value{};
};

/* These return arguments which are not flags. */

std::vector<std::string> parse_flags_vec(const std::vector<Flag *>      &flags,
                                         const std::vector<std::string> &args);
std::vector<std::string> parse_flags(const std::vector<Flag *> &flags, int argc,
                                     const char *const *argv);

void show_version();
void show_short_version();

void show_help(std::string_view program_name, const std::vector<Flag *> &flags);

void show_error(std::string_view err);

} /* namespace shit */
