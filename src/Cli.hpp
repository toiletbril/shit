#pragma once

#include "Common.hpp"

#include <string>
#include <string_view>
#include <vector>

#define ADD_FLAG(flag_list, var_name, kind, short_name, long_name,             \
                 description)                                                  \
  static shit::Flag##kind concat_literal(FLAG_, var_name){                     \
      short_name, long_name, description};                                     \
  static uchar concat_literal(t__flag_dummy_, __LINE__) =                      \
      (flag_list.emplace_back(&concat_literal(FLAG_, var_name)), 0)

namespace shit {

struct Flag
{
  enum class Kind : uint8_t
  {
    Bool,
    String,
  };

  Flag(Kind type, uchar short_name, const std::string &long_name,
       const std::string &description);

  Kind             kind() const;
  uchar            short_name() const;
  std::string_view long_name() const;
  std::string_view description() const;

protected:
  Kind        m_kind;
  uchar       m_short_name;
  std::string m_long_name;
  std::string m_description;
};

struct FlagBool : public Flag
{
  FlagBool(uchar short_name, const std::string &long_name,
           const std::string &description);

  void toggle();
  bool enabled() const;

private:
  bool m_value{};
};

struct FlagString : public Flag
{
  FlagString(uchar short_name, const std::string &long_name,
             const std::string &description);

  void             set(std::string_view v);
  std::string_view contents() const;

private:
  std::string m_value{};
};

/* Returns arguments which are not flags. */
std::vector<std::string> parse_flags(const std::vector<Flag *> &flags, int argc,
                                     const char *const *argv);

void show_version();

void show_help(std::string_view program_name, const std::vector<Flag *> &flags);

void show_error(std::string_view err);

} /* namespace shit */
