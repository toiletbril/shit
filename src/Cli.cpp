#include "Cli.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

constexpr usize HELP_WRAP_WIDTH = 80;
constexpr usize HELP_INDENT = 2;

Flag::Flag(Flag::Kind kind, char short_name, StringView long_name,
           flag_section section, StringView description)
    : m_kind(kind), m_short_name(short_name), m_section(section),
      m_long_name(long_name), m_description(description)
{}

pure fn Flag::kind() const wontthrow -> Flag::Kind { return m_kind; }

fn Flag::set_position(u32 position) throws -> void { m_position = position; }

pure fn Flag::position() const wontthrow -> usize { return m_position; }

pure fn Flag::short_name() const wontthrow -> char { return m_short_name; }

pure fn Flag::long_name() const wontthrow -> StringView { return m_long_name; }

pure fn Flag::section() const wontthrow -> flag_section { return m_section; }

pure fn Flag::description() const wontthrow -> StringView
{
  return m_description;
}

FlagBool::FlagBool(char short_name, StringView long_name, flag_section section,
                   StringView description)
    : Flag(Flag::Kind::Bool, short_name, long_name, section, description)
{}

fn FlagBool::toggle() throws -> void { m_value = !m_value; }

pure fn FlagBool::is_enabled() const wontthrow -> bool { return m_value; }

fn FlagBool::reset() throws -> void
{
  m_position = 0;
  m_value = false;
}

FlagRepeatedBool::FlagRepeatedBool(char short_name, StringView long_name,
                                   flag_section section, StringView description)
    : Flag(Flag::Kind::RepeatedBool, short_name, long_name, section,
           description)
{
  ASSERT(long_name.is_empty());
}

fn FlagRepeatedBool::increment() throws -> void { m_count++; }

pure fn FlagRepeatedBool::count() const wontthrow -> usize { return m_count; }

fn FlagRepeatedBool::reset() throws -> void
{
  m_position = 0;
  m_count = 0;
}

FlagString::FlagString(char short_name, StringView long_name,
                       flag_section section, StringView description)
    : Flag(Flag::Kind::String, short_name, long_name, section, description)
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
                                 flag_section section, StringView description)
    : Flag(Flag::Kind::ManyStrings, short_name, long_name, section, description)
{}

fn FlagManyStrings::append(StringView v) throws -> void
{
  m_values.push_managed(v);
}

pure fn FlagManyStrings::is_empty() const wontthrow -> bool
{
  return m_values.is_empty();
}

pure fn FlagManyStrings::count() const wontthrow -> usize
{
  return m_values.count();
}

pure fn FlagManyStrings::get(usize i) const wontthrow -> StringView
{
  ASSERT(i < m_values.count());
  return m_values[i].view();
}

fn FlagManyStrings::next() throws -> StringView
{
  ASSERT(m_value_position < m_values.count());
  let const &value = m_values[m_value_position++];
  return value.view();
}

pure fn FlagManyStrings::at_end() const wontthrow -> bool
{
  return m_value_position == count();
}

fn FlagManyStrings::reset() throws -> void
{
  m_position = 0;
  m_values.clear();
  m_value_position = 0;
}

static fn find_flag(const ArrayList<Flag *> &flags, const char *flag_start,
                    bool is_long, Flag **result_flag,
                    const char **value_start) throws -> bool
{
  usize longest_length = 0;

  *value_start = nullptr;
  *result_flag = nullptr;

  for (usize i = 0; i < flags.count(); ++i) {
    if (!is_long) {
      if (flags[i]->short_name() != '\0' &&
          flags[i]->short_name() == *flag_start)
      {
        *result_flag = flags[i];
        *value_start = flag_start + 1;
        return true;
      }
    } else {
      if (!flags[i]->long_name().is_empty()) {
        /* There might be flags that are prefixes of other flags. Go
           through all flags first and pick the longest match. */
        let const flag_length = flags[i]->long_name().length;

        /* strncmp stops at the argument's NUL, so a short argument such as --f
           against the flag --foobar does not read past the argument the way a
           fixed-length memcmp would. */
        if (flag_length > longest_length &&
            std::strncmp(flags[i]->long_name().data, flag_start, flag_length) ==
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
                   const ArrayList<String> &args, usize base_position,
                   const Flag *operand_value_flag) throws -> ArrayList<String>
{
  let os_argv = ArrayList<const char *>{};
  os_argv.reserve(args.count());

  for (let const &arg : args)
    os_argv.push(arg.c_str());

  return parse_flags(flags, static_cast<int>(os_argv.count()), os_argv.begin(),
                     base_position, operand_value_flag);
}

static fn flag_name(const Flag *f, bool is_long) throws -> String
{
  let name = String{};
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
               const char *const *argv, usize base_position,
               const Flag *operand_value_flag) throws -> ArrayList<String>
{
  ASSERT(argc >= 0);

  if (argc == 0) return ArrayList<String>{};

  ASSERT(argv != nullptr);

  LOG(Debug, "parsing %d command line arguments", argc);

  u32 position = 0;
  let args = ArrayList<String>{};

  Flag *prev_flag{};
  bool next_arg_is_value = false;
  bool prev_is_long = false;
  bool should_ignore_rest = false;

  for (int i = 0; i < argc; i++) {
    ASSERT(argv[i] != nullptr);

    if (next_arg_is_value) {
      /* The command flag reads its value from the first non-option operand the
         way the shell's -c does, so a recognized boolean flag that follows it
         is parsed as a flag rather than swallowed, and `-c -l command` runs
         command under -l. This applies to the command flag alone. Every other
         flag, and every builtin such as read, takes the next argument verbatim,
         so a delimiter or filename that begins with a dash is kept as the
         value. */
      bool next_is_known_bool_flag = false;
      if (prev_flag == operand_value_flag && operand_value_flag != nullptr &&
          argv[i][0] == '-' && argv[i][1] != '\0')
      {
        let const is_long_token = argv[i][1] == '-';
        let const token_offset = is_long_token ? &argv[i][2] : &argv[i][1];
        if (*token_offset != '\0') {
          Flag *probe_flag = nullptr;
          const char *probe_value = nullptr;
          if (find_flag(flags, token_offset, is_long_token, &probe_flag,
                        &probe_value) &&
              probe_flag->kind() == Flag::Kind::Bool)
          {
            next_is_known_bool_flag = true;
          }
        }
      }

      if (!next_is_known_bool_flag) {
        next_arg_is_value = false;

        ASSERT(prev_flag != nullptr);
        LOG(All,
            "attaching the next argument '%s' as the value of the flag '%s'",
            argv[i], flag_name(prev_flag, prev_is_long).c_str());
        if (prev_flag->kind() == Flag::Kind::String)
          static_cast<FlagString *>(prev_flag)->set(argv[i]);
        else
          static_cast<FlagManyStrings *>(prev_flag)->append(argv[i]);

        continue;
      }

      /* The pending value flag is kept while the recognized boolean flag below
         is parsed, so the next non-flag operand still becomes its value. */
    }

    /* The first argument is the invocation name even when it opens with a
       dash, the login convention that spawns a shell as -bash, so it is
       never read as a flag bundle. */
    if (should_ignore_rest || argv[i][0] != '-' || i == 0) {
      /* The program name is the first operand and does not end option parsing.
         The next operand is the script, after which every argument belongs to
         the script as a positional parameter, not to the shell, the way
         `sh script -x` passes -x to the script. */
      const bool is_program_name = args.is_empty();
      LOG(Debug, "taking '%s' as an operand", argv[i]);
      args.push_managed(StringView{argv[i]});
      if (!is_program_name) should_ignore_rest = true;
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

    if (*flag_offset == '\0') {
      if (is_long) {
        LOG(Debug, "stopping option parsing at '--'");
        should_ignore_rest = true;
      } else {
        args.push_managed(StringView{argv[i]});
      }

      continue;
    }

    bool should_repeat = true;

    Flag *flag{};
    const char *value_offset{};

    while (should_repeat) {
      should_repeat = false;

      let const found =
          find_flag(flags, flag_offset, is_long, &flag, &value_offset);

      if (found) {
        switch (flag->kind()) {
        case Flag::Kind::Bool: {
          let const bool_flag = static_cast<FlagBool *>(flag);

          bool_flag->toggle();
          bool_flag->set_position(++position);
          LOG(All, "toggled the flag '%s'",
              flag_name(bool_flag, is_long).c_str());

          if (!is_long && *value_offset != '\0') {
            ++flag_offset;
            should_repeat = true;
            continue;
          }
        } break;

        case Flag::Kind::RepeatedBool: {
          let const repeated_flag = static_cast<FlagRepeatedBool *>(flag);

          repeated_flag->increment();
          repeated_flag->set_position(++position);
          LOG(All, "incremented the flag '%s'",
              flag_name(repeated_flag, is_long).c_str());

          if (!is_long && *value_offset != '\0') {
            ++flag_offset;
            should_repeat = true;
            continue;
          }
        } break;

        case Flag::Kind::String:
        case Flag::Kind::ManyStrings: {
          if (*value_offset == '\0') {
            LOG(All, "the flag '%s' expects the next argument as its value",
                flag_name(flag, is_long).c_str());
            next_arg_is_value = true;
            /* The flag awaiting its value is remembered here rather than at the
               end of the loop, so a recognized boolean flag parsed before the
               value arrives does not overwrite it. */
            prev_flag = flag;
            prev_is_long = is_long;
          } else {
            /* A short flag attaches its value with no separator, a long flag
               requires '=' or a space, and a long flag with a missing separator
               is an error. */
            if (*value_offset == '=') {
              value_offset++;

              if (*value_offset != '\0') {
                if (flag->kind() == Flag::Kind::String)
                  static_cast<FlagString *>(flag)->set(value_offset);
                else
                  static_cast<FlagManyStrings *>(flag)->append(value_offset);

                flag->set_position(++position);
                LOG(All, "set the flag '%s' to '%s'",
                    flag_name(flag, is_long).c_str(), value_offset);
              } else {
                throw Error{"No value provided for '" +
                            flag_name(flag, is_long) + "' flag"};
              }
            } else if (!is_long) {
              if (flag->kind() == Flag::Kind::String)
                static_cast<FlagString *>(flag)->set(value_offset);
              else
                static_cast<FlagManyStrings *>(flag)->append(value_offset);

              flag->set_position(++position);
              LOG(All, "set the flag '%s' to the attached value '%s'",
                  flag_name(flag, is_long).c_str(), value_offset);
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
          throw Error{"Missing space between '-' and other options"};
        } else {
          /* The name before '=' is reported, trimming any attached value. */
          let error_message = String{};
          error_message += "Unknown flag '-";

          if (!is_long) {
            error_message.push(*flag_offset);
          } else {
            error_message += "-";

            const StringView flag_sv = flag_offset;
            let const equals_position = flag_sv.find_character('=');

            if (equals_position.has_value())
              error_message += flag_sv.substring_of_length(0, *equals_position);
            else
              error_message += flag_sv;
          }
          error_message += "'";

          LOG(Debug, "rejecting the unknown flag in '%s'", argv[i]);

          /* The caret points at the whole offending argument in the joined
             command line, so its offset is the length of every earlier argument
             plus one space each. */
          usize caret_offset = 0;
          for (int k = 0; k < i; k++)
            caret_offset += std::strlen(argv[k]) + 1;
          throw ErrorWithLocation{
              SourceLocation{base_position + caret_offset,
                             std::strlen(argv[i])},
              error_message
          };
        }
      }
    }
  }

  if (next_arg_is_value) {
    ASSERT(prev_flag != nullptr);
    throw Error{"No value provided for '" + flag_name(prev_flag, prev_is_long) +
                "' flag"};
  }

  return args;
}

fn join_command_line(int argc, const char *const *argv) throws -> String
{
  let s = String{};
  for (int i = 0; i < argc; i++) {
    if (i > 0) s.push(' ');
    s.append(StringView{argv[i], std::strlen(argv[i])});
  }
  return s;
}

fn reset_flags(const ArrayList<Flag *> &flags) throws -> void
{
  for (let const flag : flags) {
    flag->reset();
  }
}

cold fn show_version() throws -> void
{
  let s = String{};
  s += "Shit Shell ";
  s += utils::int_to_text(SHIT_VER_MAJOR);
  s += '.';
  s += utils::int_to_text(SHIT_VER_MINOR);
  s += '.';
  s += utils::int_to_text(SHIT_VER_PATCH);
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

cold fn show_short_version() throws -> void
{
  let s = String{};
  s += utils::int_to_text(SHIT_VER_MAJOR);
  s += '.';
  s += utils::int_to_text(SHIT_VER_MINOR);
  s += '.';
  s += utils::int_to_text(SHIT_VER_PATCH);
  s += '-';
  s += SHIT_VER_EXTRA;
  /* The build mode and the short commit hash trail the version so a reported
     binary names exactly which build and revision it is. */
  s += '-';
  s += SHIT_BUILD_MODE;
  s += '-';
  s += StringView{SHIT_COMMIT_HASH}.substring_of_length(0, 7);
  s += '\n';

  print(s);
  flush();
}

cold fn make_synopsis(StringView program_name,
                      const ArrayList<StringView> &lines) throws -> String
{
  let s = String{};

  s += "SYNOPSIS\n";

  for (let const line : lines) {
    s += "  ";
    s += program_name;
    s += ' ';
    s += line;
    s += '\n';
  }

  return s;
}

cold fn wrap_text(StringView text, usize indent, usize width) throws -> String
{
  let out = String{};
  const usize text_width = width > indent ? width - indent : 1;
  usize line_used = 0;
  usize word_start = 0;
  bool is_line_started = false;
  /* Each space, and the end of the text, closes a word. A word is placed on the
     current line when it still fits, otherwise a new indented line begins. A
     word wider than the line is emitted whole rather than split. */
  for (usize i = 0; i <= text.length; i++) {
    const bool at_end = i == text.length;
    if (!at_end && text[i] != ' ') continue;
    const usize word_length = i - word_start;
    if (word_length > 0) {
      if (is_line_started && line_used + 1 + word_length > text_width) {
        out += '\n';
        is_line_started = false;
        line_used = 0;
      }
      if (!is_line_started) {
        for (usize j = 0; j < indent; j++)
          out += ' ';
        is_line_started = true;
      } else {
        out += ' ';
        line_used++;
      }
      out += text.substring_of_length(word_start, word_length);
      line_used += word_length;
    }
    word_start = i + 1;
  }
  return out;
}

cold fn make_flag_help(const ArrayList<Flag *> &flags) throws -> String
{
  let s = String{};

  /* The description starts at a fixed column so every flag lines up, and a
     description longer than the line wraps with its continuation indented to
     the same column. A flag whose names reach the column gets its description
     on the next line. */
  static constexpr usize DESCRIPTION_COLUMN = 26;
  static constexpr usize TEXT_WIDTH = HELP_WRAP_WIDTH - DESCRIPTION_COLUMN;

  let const do_render_flag = [&](const shit::Flag *f) throws {
    s += "\n";

    /* The whole left part, the short form, the long form, and the value
       placeholder, is built first so its width decides the padding. */
    let left = String{};
    if (f->short_name() != '\0') {
      left += "  -";
      left += f->short_name();
      if (f->kind() == shit::Flag::Kind::RepeatedBool) {
        left += "[";
        left += f->short_name();
        left += "..]";
      }
      if (!f->long_name().is_empty()) left += ", ";
    } else if (!f->long_name().is_empty()) {
      left += "      ";
    } else {
      left += "  ";
    }

    if (!f->long_name().is_empty()) {
      left += "--";
      left += f->long_name();
      switch (f->kind()) {
      case shit::Flag::Kind::String: left += "=<...>"; break;
      case shit::Flag::Kind::ManyStrings: left += "=<.., ..>"; break;
      case shit::Flag::Kind::Bool:
      case shit::Flag::Kind::RepeatedBool: break;
      }
    }

    s += left;

    /* A left part that reaches the column, leaving no room for a two-space gap,
       takes the description on the next line. Otherwise it is padded to the
       column. */
    if (left.length() + 2 > DESCRIPTION_COLUMN) {
      s += '\n';
      for (usize i = 0; i < DESCRIPTION_COLUMN; i++)
        s += ' ';
    } else {
      for (usize i = left.length(); i < DESCRIPTION_COLUMN; i++)
        s += ' ';
    }

    /* Word-wrap the description so no line exceeds the wrap width, each
       continuation indented back to the description column. A single word
       longer than the text width is emitted whole rather than split. */
    let const description = f->description();
    usize line_used = 0;
    usize word_start = 0;
    for (usize i = 0; i <= description.length; i++) {
      const bool at_end = i == description.length;
      if (!at_end && description[i] != ' ') continue;

      const usize word_length = i - word_start;
      if (word_length > 0) {
        if (line_used > 0 && line_used + 1 + word_length > TEXT_WIDTH) {
          s += '\n';
          for (usize j = 0; j < DESCRIPTION_COLUMN; j++)
            s += ' ';
          line_used = 0;
        }
        if (line_used > 0) {
          s += ' ';
          line_used++;
        }
        s += description.substring_of_length(word_start, word_length);
        line_used += word_length;
      }
      word_start = i + 1;
    }
  };

  /* Each flag renders under the section its definition names, in enum order,
     the NoSection flags first under the plain OPTIONS heading. */
  static const StringView SECTION_HEADERS[] = {
      "OPTIONS",      "POSIX OPTIONS", "BASH OPTIONS", "COMPATIBILITY OPTIONS",
      "SHIT OPTIONS", "DEBUG OPTIONS"};
  for (u8 section = 0; section < countof(SECTION_HEADERS); section++) {
    bool was_header_printed = false;
    for (let const flag : flags) {
      if (static_cast<u8>(flag->section()) != section) continue;
      if (!was_header_printed) {
        if (!s.is_empty()) s += "\n\n";
        s += SECTION_HEADERS[section];
        was_header_printed = true;
      }
      do_render_flag(flag);
    }
  }

  return s;
}

fn print(StringView text) throws -> void
{
  /* The output is flushed at once so it interleaves with the unbuffered
     write_fd path the builtins use, keeping the order a reader sees correct. */
  std::fwrite(text.data, 1, text.count(), stdout);
  std::fflush(stdout);
}

fn print_error(StringView text) throws -> void
{
  std::fwrite(text.data, 1, text.count(), stderr);
  std::fflush(stderr);
}

fn flush() throws -> void { std::fflush(stdout); }

/* Set while the editor sits mid-line and a diagnostic must break to its own
   line first. The first show_message consumes it, so only the leading message
   of a completion run is pushed down, not every message after it. */
static thread_local bool MESSAGE_LEADING_NEWLINE_ARMED = false;

fn arm_message_leading_newline(bool armed) wontthrow -> void
{
  MESSAGE_LEADING_NEWLINE_ARMED = armed;
}

cold fn show_message(StringView err) throws -> void
{
  if (MESSAGE_LEADING_NEWLINE_ARMED) {
    print_error("\n");
    MESSAGE_LEADING_NEWLINE_ARMED = false;
  }
  print_error("shit: ");
  print_error(err);
  print_error("\n");
}

} // namespace shit
