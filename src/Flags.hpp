#pragma once

#include "Common.hpp"

#include <string>
#include <string_view>
#include <vector>

enum class FlagType
{
  Bool,
  String,
};

struct Flag
{
  Flag(FlagType type, uchar short_name, std::string long_name,
       std::string description);

  FlagType         type() const;
  uchar            short_name() const;
  std::string_view long_name() const;
  std::string_view description() const;

protected:
  FlagType    m_type;
  uchar       m_short_name;
  std::string m_long_name;
  std::string m_description;
};

struct FlagBool : public Flag
{
  FlagBool(uchar short_name, std::string long_name, std::string description);

  void toggle();
  bool enabled() const;

private:
  bool m_value{};
};

struct FlagString : public Flag
{
  FlagString(uchar short_name, std::string long_name, std::string description);

  void             set(std::string_view v);
  std::string_view contents() const;

private:
  std::string m_value{};
};

/* Returns arguments which are not flags. */
std::vector<std::string> flag_parse(std::vector<Flag *> &flags, int argc,
                                    char **argv);
