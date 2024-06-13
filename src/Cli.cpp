#include "Cli.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace shit {

/**
 * class: Flag
 */
Flag::Flag(Flag::Kind kind, char short_name, const std::string &long_name,
           const std::string &description)
    : m_kind(kind), m_short_name(short_name), m_long_name(long_name),
      m_description(description)
{}

Flag::Kind
Flag::kind() const
{
  return m_kind;
}

char
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
FlagBool::FlagBool(char short_name, const std::string &long_name,
                   const std::string &description)
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
FlagString::FlagString(char short_name, const std::string &long_name,
                       const std::string &description)
    : Flag(Flag::Kind::String, short_name, long_name, description)
{}

void
FlagString::set(std::string_view v)
{
  m_value = v;
  m_was_set = true;
}

bool
FlagString::was_set() const
{
  return m_was_set;
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

  *value_start = nullptr;
  *result_flag = nullptr;

  for (size_t i = 0; i < flags.size(); ++i) {
    if (!is_long) {
      if (flags[i]->short_name() != '\0' &&
          flags[i]->short_name() == *flag_start)
      {
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
parse_flags_vec(const std::vector<Flag *>      &flags,
                const std::vector<std::string> &args)
{
  std::vector<const char *> os_argv;
  os_argv.reserve(args.size());
  for (const std::string &arg : args) {
    os_argv.emplace_back(arg.c_str());
  }
  return parse_flags(flags, os_argv.size(),
                     const_cast<char const *const *>(os_argv.data()));
}

static std::string
flag_name(const Flag *f, bool is_long)
{
  return "-" + (is_long ? "-" + std::string{f->long_name()}
                        : std::string{f->short_name()});
}

std::vector<std::string>
parse_flags(const std::vector<Flag *> &flags, int argc, const char *const *argv)
{
  SHIT_ASSERT(argc >= 0);

  if (argc == 0) {
    return {};
  }

  SHIT_ASSERT(argv);

  std::vector<std::string> args{};

  Flag *prev_flag{};
  bool  next_arg_is_value = false;
  bool  prev_is_long = false;
  bool  ignore_rest = false;

  for (int i = 0; i < argc; i++) {
    SHIT_ASSERT(argv[i]);

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
    const char *flag_offset{};

    if (argv[i][1] != '-') {
      flag_offset = &argv[i][1];
    } else {
      flag_offset = &argv[i][2];
      is_long = true;
    }

    if (*flag_offset == '\0') {
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
    const char *value_offset{};

    while (repeat) {
      repeat = false;

      bool found = find_flag(flags, flag_offset, is_long, &flag, &value_offset);

      if (found) {
        switch (flag->kind()) {
        case Flag::Kind::Bool: {
          static_cast<FlagBool *>(flag)->toggle();

          /* Check for combined flags, e.g -vAsn. */
          if (!is_long && *value_offset != '\0') {
            ++flag_offset;
            repeat = true;
            continue;
          }
        } break;

        case Flag::Kind::String: {
          FlagString *fs = static_cast<FlagString *>(flag);

          if (*value_offset == '\0') {
            /* There is nothing after the flag. Expect next argument to be the
             * value. */
            next_arg_is_value = true;
          } else {
            /* Check for a separator. Short flags do not require a separator
               between the flag and the value, but long flags require a space or
               '='. Treat missing separator for long flags as an error. */
            if (*value_offset == '=') {
              value_offset++;

              /* Value is provided with '='. */
              if (*value_offset != '\0') {
                fs->set(value_offset);
              } else {
                throw Error{"No value provided for '" +
                            flag_name(flag, is_long) + "'"};
              }
            } else if (!is_long) {
              /* Flag is short, value is provided without a separator. */
              fs->set(value_offset);
            } else {
              throw Error{
                  "Long flags require a separator between the flag and the "
                  "value. Try using '" +
                  flag_name(flag, is_long) + "=" + value_offset + "'"};
            }
          }
        } break;
        }
      }

      if (!found) {
        if (*flag_offset == '-') {
          /* '-E-c' */
          throw Error{"Missing space between '-' and other options"};
        } else {
          /* Trim the value before '=' and report unknown flag. */
          std::string s;
          s += "Unknown flag '-";

          if (!is_long) {
            s += std::string{*flag_offset};
          } else {
            s += "-";

            std::string_view flag_sv = flag_offset;
            usize            equals_pos = flag_sv.find('=');

            if (equals_pos != std::string::npos) {
              s += flag_sv.substr(0, equals_pos);
            } else {
              s += flag_sv;
            }
          }
          s += "'";

          throw Error{s};
        }
      }
    }

    prev_flag = flag;
    prev_is_long = is_long;
  }

  /* Previous flag expected a value after space. */
  if (next_arg_is_value) {
    throw Error{"No value provided for '" + flag_name(prev_flag, prev_is_long) +
                "'"};
  }

  return args;
}

void
show_version()
{
  std::cout
      << "Shit Shell " << SHIT_VER_MAJOR << '.' << SHIT_VER_MINOR << '.'
      << SHIT_VER_PATCH << '-' << SHIT_VER_EXTRA << '\n'
      << "(c) toiletbril <https://github.com/toiletbril>\n\n" SHIT_SHORT_LICENSE
      << std::endl;
}

void
show_short_version()
{
  std::cout << SHIT_VER_MAJOR << '.' << SHIT_VER_MINOR << '.' << SHIT_VER_PATCH
            << std::endl;
}

void
show_help(std::string_view program_name, const std::vector<Flag *> &flags)
{
  std::string s;

  static constexpr usize MAX_WIDTH = 24;
  static constexpr usize LONG_PADDING = 6;

  s += "SYNOPSIS\n";

  s += "  ";
  s += program_name;
  s += ' ';
  s += "[-OPTIONS] [--] <file1> [file2, ...]\n";

  s += "  ";
  s += program_name;
  s += ' ';
  s += "[-OPTIONS] -\n";

  s += "  ";
  s += program_name;
  s += ' ';
  s += "[-OPTIONS]\n";

  s += '\n';
  s += "OPTIONS";
  for (const shit::Flag *f : flags) {
    s += "\n";

    bool has_short = false;
    bool long_is_string = false;

    /* '-E' */
    if (f->short_name() != '\0') {
      s += "  -";
      s += f->short_name();
      has_short = true;
    }

    if (!f->long_name().empty()) {
      if (has_short) {
        /* '-E, ' */
        s += ", ";
      } else {
        /* Only long flag exists, replace '  -E, ' with 6 spaces. */
        s += "      ";
      }

      /* '-E, --exit-code' */
      s += "--";
      s += f->long_name();
      if (f->kind() == shit::Flag::Kind::String) {
        /* '-E, --exit-code=<...>' */
        s += "=<...>";
        long_is_string = true;
      }
    }

    usize padding = MAX_WIDTH - f->long_name().length() -
                    (long_is_string ? LONG_PADDING : 0);

    for (usize i = 0; i < padding; i++) {
      s += ' ';
    }

    /* NOTE: This does not wrap long descriptions. */
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
