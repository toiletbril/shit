#include "Cli.hpp"

#include "Common.hpp"
#include "Errors.hpp"

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace shit {

/**
 * class: Flag
 */
Flag::Flag(Flag::Kind kind, uchar short_name, std::string long_name,
           std::string description)
    : m_kind(kind), m_short_name(short_name), m_long_name(long_name),
      m_description(description)
{}

Flag::Kind
Flag::kind() const
{
  return m_kind;
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
    : Flag(Flag::Kind::Bool, short_name, long_name, description)
{}

void
FlagBool::toggle()
{
  m_value = !m_value;
}

bool
FlagBool::enabled() const
{
  return m_value;
}

/**
 * class: FlagBool
 */
FlagString::FlagString(uchar short_name, std::string long_name,
                       std::string description)
    : Flag(Flag::Kind::String, short_name, long_name, description)
{}

void
FlagString::set(std::string_view v)
{
  m_value = v;
}

std::string_view
FlagString::contents() const
{
  return m_value;
}

static bool
find_flag(const std::vector<Flag *> &flags, const char *flag_start,
          bool is_long, Flag **result_flag, const char **value_start)
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
                        flag_length) == 0)
        {
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
parse_flags(const std::vector<Flag *> &flags, int argc, const char *const *argv)
{
  if (argc <= 0 || argv == NULL)
    throw Error{"Invalid arguments to flag_parse()"};

  std::vector<std::string> args{};

  Flag *prev_flag{};
  bool  next_arg_is_value = false;
  bool  prev_is_long = false;
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

    bool        is_long = false;
    const char *flag_start{};

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

    Flag       *flag{};
    const char *value_start{};

    while (repeat) {
      repeat = false;
      bool found = find_flag(flags, flag_start, is_long, &flag, &value_start);

      if (found) {
        switch (flag->kind()) {
        case Flag::Kind::Bool: {
          FlagBool *fb = static_cast<FlagBool *>(flag);
          fb->toggle();
          /* Check for combined flags, e.g -vAsn. */
          if (!is_long && *value_start != '\0') {
            ++flag_start;
            repeat = true;
            continue;
          }
        } break;

        case Flag::Kind::String: {
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
        if (*flag_start == '-')
          throw Error{"Missing space between '-' of the options."};
        else {
          std::string s;
          s += "Unknown flag '-";

          if (!is_long) {
            s += std::string{*flag_start};
          } else {
            s += "-";

            std::string_view flag = flag_start;
            usize            equals_pos = flag.find("=");

            if (equals_pos != std::string::npos)
              s += flag.substr(0, equals_pos);
            else
              s += flag;
          }

          s += "'";
          throw Error{s};
        }
      }
    }

    prev_flag = flag;
    prev_is_long = is_long;
  }

  if (next_arg_is_value) {
    std::string s;
    s += "Unknown flag '-";
    if (prev_is_long)
      s += "-" + std::string{prev_flag->long_name()};
    else
      s += static_cast<char>(prev_flag->short_name());
    s += "'";
    throw Error{s};
  }

  return args;
}

void
show_version()
{
  std::cout
      << "Shit " << SHIT_VER_MAJOR << '.' << SHIT_VER_MINOR << '.'
      << SHIT_VER_PATCH << "\n"
      << "(c) toiletbril <https://github.com/toiletbril>\n\n"
         "License GPLv3: GNU GPL version 3.\n"
         "This is free software: you are free to change and redistribute it.\n"
         "There is NO WARRANTY, to the extent permitted by law."
      << std::endl;
}

void
show_help(std::string_view program_name, const std::vector<Flag *> flags)
{
  std::string s;

  s += "Usage:\n";
  s += "  ";
  s += program_name;
  s += " [-options]";
  s += " [file1, ...]\n";
  s += "  ";
  s += "Command-line interpreter or shell.";
  s += "\n\n";

  s += "Options:";
  for (const shit::Flag *f : flags) {
    s += "\n";
    bool has_short = false;
    bool long_is_string = false;
    if (f->short_name() != '\0') {
      s += "  -";
      s += f->short_name();
      has_short = true;
    }
    if (!f->long_name().empty()) {
      if (has_short)
        s += ", ";
      else
        s += "      ";
      s += "--";
      s += f->long_name();
      if (f->kind() == shit::Flag::Kind::String) {
        s += "=<...>";
        long_is_string = true;
      }
    }
    usize padding = 24 - f->long_name().length() - (long_is_string ? 6 : 0);
    for (usize i = 0; i < padding; i++)
      s += ' ';
    s += f->description();
  }
  std::cerr << s << std::endl;
}

void
show_error(std::string_view err)
{
  std::cerr << "shit: " << err << std::endl;
}

} /* namespace shit */
