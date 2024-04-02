#include "Flags.hpp"

#include "Common.hpp"
#include "Errors.hpp"

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

/**
 * class: Flag
 */
Flag::Flag(FlagType type, uchar short_name, std::string long_name,
           std::string description)
    : m_type(type), m_short_name(short_name), m_long_name(long_name),
      m_description(description)
{
}

FlagType
Flag::type() const
{
  return m_type;
}

uchar
Flag::short_name() const
{
  return m_short_name;
}

std::string_view
Flag::long_name() const
{
  return m_long_name;
}

std::string_view
Flag::description() const
{
  return m_description;
}

/**
 * class: FlagBool
 */
FlagBool::FlagBool(uchar short_name, std::string long_name,
                   std::string description)
    : Flag(FlagType::Bool, short_name, long_name, description)
{
}

void
FlagBool::toggle()
{
  m_value = !m_value;
}

bool
FlagBool::get() const
{
  return m_value;
}

/**
 * class: FlagBool
 */
FlagString::FlagString(uchar short_name, std::string long_name,
                       std::string description)
    : Flag(FlagType::String, short_name, long_name, description)
{
}

void
FlagString::set(std::string_view v)
{
  m_value = v;
}

std::string_view
FlagString::get() const
{
  return m_value;
}

static bool
find_flag(std::vector<Flag *> &flags, const char *flag_start, bool is_long,
          Flag **result_flag, const char **value_start)
{
  size_t longest_length = 0;

  *value_start = NULL;
  *result_flag = NULL;

  for (size_t i = 0; i < flags.size(); ++i) {
    if (!is_long) {
      if (flags[i]->short_name() != '\0' &&
          flags[i]->short_name() == *flag_start) {
        *result_flag = flags[i];
        *value_start = flag_start + 1;
        return true;
      }
    } else {
      if (!flags[i]->long_name().empty()) {
        /* There might be flags that are prefixes of other flags. Go through all
           flags first and pick the longest match. */
        size_t flag_length = flags[i]->long_name().length();

        if (flag_length > longest_length &&
            /* Yay let's add starts_with in C++20. */
            std::memcmp(flags[i]->long_name().data(), flag_start,
                        flag_length) == 0) {
          *result_flag = flags[i];
          *value_start = flag_start + flag_length;
          longest_length = flag_length;
        }
      }
    }
  }

  return longest_length > 0;
}

std::vector<std::string>
flag_parse(std::vector<Flag *> &flags, int argc, char **argv)
{
  if (argc <= 0 || argv == NULL)
    throw Error{"Invalid arguments to flag_parse()"};

  std::vector<std::string> args{};

  Flag *prev_flag{};
  bool  next_arg_is_value = false;
  bool  ignore_rest = false;

  for (int i = 0; i < argc; i++) {
    if (next_arg_is_value) {
      next_arg_is_value = false;
      static_cast<FlagString *>(prev_flag)->set(argv[i]);
      continue;
    }

    if (ignore_rest || argv[i][0] != '-') {
      args.push_back(argv[i]);
      continue;
    }

    bool  is_long = false;
    char *flag_start{};

    if (argv[i][1] != '-')
      flag_start = &argv[i][1];
    else {
      flag_start = &argv[i][2];
      is_long = true;
    }

    if (*flag_start == '\0') {
      if (is_long) {
        /* Skip the rest of the flags after '--'. */
        ignore_rest = true;
      } else {
        /* Treat '-' as an argument. */
        args.push_back(argv[i]);
      }
      continue;
    }

    bool repeat = true;

    Flag       *flag;
    const char *value_start;

    while (repeat) {
      repeat = false;
      bool found = find_flag(flags, flag_start, is_long, &flag, &value_start);

      if (found) {
        switch (flag->type()) {
        case FlagType::Bool: {
          FlagBool *fb = static_cast<FlagBool *>(flag);
          fb->toggle();
          /* Check for combined flags, e.g -vAsn. */
          if (!is_long && *value_start != '\0') {
            ++flag_start;
            repeat = true;
            continue;
          }
        } break;

        case FlagType::String: {
          FlagString *fs = static_cast<FlagString *>(flag);

          if (*value_start == '\0')
            next_arg_is_value = true;
          else {
            /* Check for a separator. Short flags do not require a separator
               between the flag and the value, but long flags do. Treat missing
               separator for long flags as an error. */
            if (*value_start == '=') {
              value_start++;
              fs->set(value_start);
            } else if (!is_long) {
              fs->set(value_start);
            } else {
              found = false;
            }
          }
        } break;
        }
      }

      if (!found) {
        std::string s;
        s += "Unknown flag '-";
        s += is_long ? "-" + std::string{flag_start} : std::string{*flag_start};
        s += "'";
        throw Error{s};
      }
    }

    prev_flag = flag;
  }

  if (next_arg_is_value)
    throw Error{"No value provided for " + std::string{prev_flag->long_name()}};

  return args;
}
