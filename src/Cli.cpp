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

void
Flag::set_position(u32 n)
{
  m_position = n;
}

usize
Flag::position() const
{
  return m_position;
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
FlagBool::is_enabled() const
{
  return m_value;
}

void
FlagBool::reset()
{
  m_position = 0;
  m_value = false;
}

/**
 * class: FlagString
 */
FlagString::FlagString(char short_name, const std::string &long_name,
                       const std::string &description)
    : Flag(Flag::Kind::String, short_name, long_name, description)
{}

void
FlagString::set(std::string_view v)
{
  m_value = v;
  m_is_set = true;
}

bool
FlagString::is_set() const
{
  return m_is_set;
}

std::string_view
FlagString::value() const
{
  return m_value;
}

void
FlagString::reset()
{
  m_position = 0;
  m_value.clear();
  m_is_set = false;
}

/**
 * class: FlagManyStrings
 */
FlagManyStrings::FlagManyStrings(char short_name, const std::string &long_name,
                                 const std::string &description)
    : Flag(Flag::Kind::ManyStrings, short_name, long_name, description)
{}

void
FlagManyStrings::append(std::string_view v)
{
  m_values.emplace_back(v);
}

bool
FlagManyStrings::is_empty() const
{
  return m_values.empty();
}

usize
FlagManyStrings::size() const
{
  return m_values.size();
}

std::string_view
FlagManyStrings::get(usize i) const
{
  return m_values[i];
}

std::string_view
FlagManyStrings::next()
{
  return m_values[m_value_position++];
}

bool
FlagManyStrings::at_end() const
{
  return m_value_position == size();
}

void
FlagManyStrings::reset()
{
  m_position = 0;
  m_values.clear();
  m_value_position = 0;
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

  u32                      position = 0;
  std::vector<std::string> args{};

  Flag *prev_flag{};
  bool  next_arg_is_value = false;
  bool  prev_is_long = false;
  bool  ignore_rest = false;

  for (int i = 0; i < argc; i++) {
    SHIT_ASSERT(argv[i]);

    if (next_arg_is_value) {
      next_arg_is_value = false;
      if (prev_flag->kind() == Flag::Kind::String) {
        static_cast<FlagString *>(prev_flag)->set(argv[i]);
      } else {
        static_cast<FlagManyStrings *>(prev_flag)->append(argv[i]);
      }
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
          FlagBool *fb = static_cast<FlagBool *>(flag);

          fb->toggle();
          fb->set_position(++position);

          /* Check for combined flags, e.g -vAsn. */
          if (!is_long && *value_offset != '\0') {
            ++flag_offset;
            repeat = true;
            continue;
          }
        } break;

        case Flag::Kind::String:
        case Flag::Kind::ManyStrings: {
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
                if (flag->kind() == Flag::Kind::String) {
                  static_cast<FlagString *>(flag)->set(value_offset);
                } else {
                  static_cast<FlagManyStrings *>(flag)->append(value_offset);
                }
                flag->set_position(++position);
              } else {
                throw Error{"No value provided for '" +
                            flag_name(flag, is_long) + "'"};
              }
            } else if (!is_long) {
              /* Flag is short, value is provided without a separator. */
              if (flag->kind() == Flag::Kind::String) {
                static_cast<FlagString *>(flag)->set(value_offset);
              } else {
                static_cast<FlagManyStrings *>(flag)->append(value_offset);
              }
              flag->set_position(++position);
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
reset_flags(const std::vector<Flag *> &flags)
{
  for (Flag *f : flags) {
    f->reset();
  }
}

void
show_version()
{
  std::cout << "Shit Shell " << SHIT_VER_MAJOR << '.' << SHIT_VER_MINOR << '.'
            << SHIT_VER_PATCH << '-' << SHIT_VER_EXTRA << '\n'
            << "Built on " << SHIT_BUILD_DATE << '\n'
            << '\n'
            << "MODE=" << SHIT_BUILD_MODE << '\n'
            << "HEAD " << SHIT_COMMIT_HASH << '\n'
            << "Compiler " << SHIT_COMPILER << '\n'
            << "OS " << SHIT_OS_INFO << '\n'
            << '\n'
            << SHIT_SHORT_LICENSE << '\n'
            << "(c) toiletbril "
               "<https://github.com/toiletbril>"
            << std::endl;
}

void
show_short_version()
{
  std::cout << SHIT_VER_MAJOR << '.' << SHIT_VER_MINOR << '.' << SHIT_VER_PATCH
            << '-' << SHIT_VER_EXTRA << std::endl;
}

std::string
make_synopsis(std::string_view                program_name,
              const std::vector<std::string> &lines)
{
  std::string s{};

  s += "SYNOPSIS\n";

  for (std::string_view l : lines) {
    s += "  ";
    s += program_name;
    s += ' ';
    s += l;
    s += '\n';
  }

  return s;
}

std::string
make_flag_help(const std::vector<Flag *> &flags)
{
  std::string s{};

  static constexpr usize MAX_WIDTH = 24;
  static constexpr usize LONG_PADDING = 9;

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

      switch (f->kind()) {
      /* '-E, --exit-code=<...>' */
      case shit::Flag::Kind::String:
        s += "=<...>   ";
        long_is_string = true;
        break;
      /* '-E, --exit-code=<.., ..>' */
      case shit::Flag::Kind::ManyStrings:
        s += "=<.., ..>";
        long_is_string = true;
      case shit::Flag::Kind::Bool: break;
      }
    } else {
      s += "    ";
    }

    usize padding = MAX_WIDTH - f->long_name().length() -
                    (long_is_string ? LONG_PADDING : 0);

    for (usize i = 0; i < padding; i++) {
      s += ' ';
    }

    /* NOTE: This does not wrap long descriptions. */
    s += f->description();
  }

  return s;
}

void
show_message(std::string_view err)
{
  std::cerr << "shit: " << err << std::endl;
}

} /* namespace shit */
