#include "Cli.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Utils.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace shit {

/* TODO: Make CLI tests. */

Flag::Flag(Flag::Kind kind, char short_name, StringView long_name,
           StringView description)
    : m_kind(kind), m_short_name(short_name), m_long_name(long_name),
      m_description(description)
{}

pure fn Flag::kind() const wontthrow -> Flag::Kind { return m_kind; }

fn Flag::set_position(u32 n) throws -> void { m_position = n; }

pure fn Flag::position() const wontthrow -> usize { return m_position; }

pure fn Flag::short_name() const wontthrow -> char { return m_short_name; }

pure fn Flag::long_name() const wontthrow -> StringView { return m_long_name; }

pure fn Flag::description() const wontthrow -> StringView
{
  return m_description;
}

FlagBool::FlagBool(char short_name, StringView long_name,
                   StringView description)
    : Flag(Flag::Kind::Bool, short_name, long_name, description)
{}

fn FlagBool::toggle() throws -> void { m_value = !m_value; }

pure fn FlagBool::is_enabled() const wontthrow -> bool { return m_value; }

fn FlagBool::reset() throws -> void
{
  m_position = 0;
  m_value = false;
}

FlagString::FlagString(char short_name, StringView long_name,
                       StringView description)
    : Flag(Flag::Kind::String, short_name, long_name, description)
{}

fn FlagString::set(StringView v) throws -> void
{
  m_value = v;
  m_is_set = true;
}

pure fn FlagString::is_set() const wontthrow -> bool { return m_is_set; }

pure fn FlagString::value() const wontthrow -> StringView
{
  return m_value.view();
}

fn FlagString::reset() throws -> void
{
  m_position = 0;
  m_value.clear();
  m_is_set = false;
}

FlagManyStrings::FlagManyStrings(char short_name, StringView long_name,
                                 StringView description)
    : Flag(Flag::Kind::ManyStrings, short_name, long_name, description)
{}

fn FlagManyStrings::append(StringView v) throws -> void
{
  m_values.push(String{heap_allocator(), v});
}

pure fn FlagManyStrings::is_empty() const wontthrow -> bool
{
  return m_values.empty();
}

pure fn FlagManyStrings::size() const wontthrow -> usize
{
  return m_values.size();
}

pure fn FlagManyStrings::get(usize i) const wontthrow -> StringView
{
  ASSERT(i < m_values.size());
  return m_values[i].view();
}

fn FlagManyStrings::next() throws -> StringView
{
  ASSERT(m_value_position < m_values.size());
  const String &value = m_values[m_value_position++];
  return value.view();
}

pure fn FlagManyStrings::at_end() const wontthrow -> bool
{
  return m_value_position == size();
}

fn FlagManyStrings::reset() throws -> void
{
  m_position = 0;
  m_values.clear();
  m_value_position = 0;
}

static fn find_flag(const ArrayList<Flag *> &flags, const char *flag_start,
                    bool is_long, Flag **result_flag, const char **value_start)
    throws -> bool
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
        /* There might be flags that are prefixes of other flags. Go
           through all flags first and pick the longest match. */
        const size_t flag_length = flags[i]->long_name().length;

        if (flag_length > longest_length &&
            /* Yay let's add starts_with in C++20. */
            std::memcmp(flags[i]->long_name().data, flag_start, flag_length) ==
                0)
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

fn parse_flags_vec(const ArrayList<Flag *> &flags,
                   const ArrayList<String> &args) throws -> ArrayList<String>
{
  std::vector<const char *> os_argv;
  os_argv.reserve(args.size());

  for (const String &arg : args)
    os_argv.emplace_back(arg.c_str());

  return parse_flags(flags, os_argv.size(),
                     const_cast<char const *const *>(os_argv.data()));
}

static fn flag_name(const Flag *f, bool is_long) throws -> String
{
  String name{};
  name += "-";
  if (is_long) {
    name += "-";
    name += f->long_name();
  } else {
    name.push(f->short_name());
  }
  return name;
}

fn parse_flags(const ArrayList<Flag *> &flags, int argc,
               const char *const *argv) throws -> ArrayList<String>
{
  ASSERT(argc >= 0);

  if (argc == 0) return ArrayList<String>{};

  ASSERT(argv != nullptr);

  u32 position = 0;
  ArrayList<String> args{};

  Flag *prev_flag{};
  bool next_arg_is_value = false;
  bool prev_is_long = false;
  bool ignore_rest = false;

  for (int i = 0; i < argc; i++) {
    ASSERT(argv[i] != nullptr);

    if (next_arg_is_value) {
      next_arg_is_value = false;

      ASSERT(prev_flag != NULL);
      if (prev_flag->kind() == Flag::Kind::String)
        static_cast<FlagString *>(prev_flag)->set(argv[i]);
      else
        static_cast<FlagManyStrings *>(prev_flag)->append(argv[i]);

      continue;
    }

    if (ignore_rest || argv[i][0] != '-') {
      args.push(String{heap_allocator(), StringView{argv[i]}});
      continue;
    }

    bool is_long = false;
    const char *flag_offset{};

    if (argv[i][1] != '-') {
      flag_offset = &argv[i][1];
    } else {
      flag_offset = &argv[i][2];
      is_long = true;
    }

    /* Skip the rest of the flags after '--' or treat '-' as an argument. */
    if (*flag_offset == '\0') {
      if (is_long)
        ignore_rest = true;
      else
        args.push(String{heap_allocator(), StringView{argv[i]}});

      continue;
    }

    bool repeat = true;

    Flag *flag{};
    const char *value_offset{};

    while (repeat) {
      repeat = false;

      let const found =
          find_flag(flags, flag_offset, is_long, &flag, &value_offset);

      if (found) {
        switch (flag->kind()) {
        case Flag::Kind::Bool: {
          let const fb = static_cast<FlagBool *>(flag);

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
            /* There is None after the flag. Expect next argument
             * to be the value. */
            next_arg_is_value = true;
          } else {
            /* Check for a separator. Short flags do not require a
               separator between the flag and the value, but long
               flags require a space or
               '='. Treat missing separator for long flags as an
               error. */
            if (*value_offset == '=') {
              value_offset++;

              /* Value is provided with '='. */
              if (*value_offset != '\0') {
                if (flag->kind() == Flag::Kind::String)
                  static_cast<FlagString *>(flag)->set(value_offset);
                else
                  static_cast<FlagManyStrings *>(flag)->append(value_offset);

                flag->set_position(++position);
              } else {
                throw Error{"No value provided for '" +
                            flag_name(flag, is_long) + "' flag"};
              }
            } else if (!is_long) {
              /* Flag is short, value is provided without a
               * separator. */
              if (flag->kind() == Flag::Kind::String)
                static_cast<FlagString *>(flag)->set(value_offset);
              else
                static_cast<FlagManyStrings *>(flag)->append(value_offset);

              flag->set_position(++position);
            } else {
              throw Error{"Long flags require a separator "
                          "between the flag and the "
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
          String s{};
          s += "Unknown flag '-";

          if (!is_long) {
            s.push(*flag_offset);
          } else {
            s += "-";

            const StringView flag_sv = flag_offset;
            let const equals_pos = flag_sv.find_character('=');

            if (equals_pos)
              s += flag_sv.substring_of_length(0, equals_pos.value());
            else
              s += flag_sv;
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
    ASSERT(prev_flag != NULL);
    throw Error{"No value provided for '" + flag_name(prev_flag, prev_is_long) +
                "' flag"};
  }

  return args;
}

fn reset_flags(const ArrayList<Flag *> &flags) throws -> void
{
  for (Flag *f : flags) {
    f->reset();
  }
}

fn show_version() throws -> void
{
  String s{};
  s += "Shit Shell ";
  s += utils::integer_to_string(SHIT_VER_MAJOR);
  s += '.';
  s += utils::integer_to_string(SHIT_VER_MINOR);
  s += '.';
  s += utils::integer_to_string(SHIT_VER_PATCH);
  s += '-';
  s += SHIT_VER_EXTRA;
  s += '\n';
  s += "Built on ";
  s += SHIT_BUILD_DATE;
  s += '\n';
  s += '\n';
  s += "MODE=";
  s += SHIT_BUILD_MODE;
  s += '\n';
  s += "HEAD=";
  s += SHIT_COMMIT_HASH;
  s += '\n';
  s += "CXX=";
  s += SHIT_COMPILER;
  s += '\n';
  s += "ENVCXXFLAGS=";
  s += (*SHIT_ENVCXXFLAGS == '\0' ? "<none>" : SHIT_ENVCXXFLAGS);
  s += '\n';
  s += "OS=";
  s += SHIT_OS_INFO;
  s += '\n';
  s += '\n';
  s += SHIT_SHORT_LICENSE;
  s += '\n';
  s += "(c) toiletbril <https://github.com/toiletbril>";
  s += '\n';

  print(s);
  flush();
}

fn show_short_version() throws -> void
{
  String s{};
  s += utils::integer_to_string(SHIT_VER_MAJOR);
  s += '.';
  s += utils::integer_to_string(SHIT_VER_MINOR);
  s += '.';
  s += utils::integer_to_string(SHIT_VER_PATCH);
  s += '-';
  s += SHIT_VER_EXTRA;
  s += '\n';

  print(s);
  flush();
}

fn make_synopsis(std::string_view program_name,
                 const std::vector<std::string> &lines) throws -> std::string
{
  String s{};

  s += "SYNOPSIS\n";

  for (StringView l : lines) {
    s += "  ";
    s += StringView{program_name.data(), program_name.size()};
    s += ' ';
    s += l;
    s += '\n';
  }

  return std::string{s.c_str(), s.size()};
}

fn make_flag_help(const ArrayList<Flag *> &flags) throws -> std::string
{
  String s{};

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

    const usize padding =
        MAX_WIDTH - f->long_name().length - (long_is_string ? LONG_PADDING : 0);

    for (usize i = 0; i < padding; i++) {
      s += ' ';
    }

    /* NOTE: This does not wrap long descriptions. */
    s += f->description();
  }

  return std::string{s.c_str(), s.size()};
}

fn print(StringView text) throws -> void
{
  /* The output is flushed at once so it interleaves with the unbuffered
     write_fd path the builtins use, keeping the order a reader sees correct. */
  std::fwrite(text.data, 1, text.size(), stdout);
  std::fflush(stdout);
}

fn print_error(StringView text) throws -> void
{
  std::fwrite(text.data, 1, text.size(), stderr);
  std::fflush(stderr);
}

fn flush() throws -> void { std::fflush(stdout); }

fn show_message(StringView err) throws -> void
{
  print_error("shit: ");
  print_error(err);
  print_error("\n");
}

} /* namespace shit */
