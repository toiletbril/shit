#include "Eval.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>

namespace shit {

EvalContext::EvalContext(bool should_disable_path_expansion, bool should_echo,
                         bool should_echo_expanded, bool shell_is_interactive,
                         bool should_error_exit, std::string shell_name,
                         std::vector<std::string> positional_params)
    : m_shell_name(std::move(shell_name)),
      m_positional_params(std::move(positional_params)),
      m_enable_path_expansion(!should_disable_path_expansion),
      m_enable_echo(should_echo), m_enable_echo_expanded(should_echo_expanded),
      m_shell_is_interactive(shell_is_interactive),
      m_error_exit(should_error_exit)
{}

void
EvalContext::add_evaluated_expression()
{
  m_expressions_executed_last++;
}

void
EvalContext::add_expansion()
{
  m_expansions_last++;
}

void
EvalContext::end_command()
{
  m_expansions_total += m_expansions_last;
  m_expressions_executed_total += m_expressions_executed_last;

  m_expansions_last = m_expressions_executed_last = 0;
}

void
EvalContext::set_shell_variable(const std::string &name, std::string value)
{
  m_shell_variables[name] = std::move(value);
}

void
EvalContext::unset_shell_variable(const std::string &name)
{
  m_shell_variables.erase(name);
}

std::optional<std::string>
EvalContext::get_variable_value(const std::string &name) const
{
  if (name == "?") return std::to_string(m_last_exit_status);
  if (name == "$") return std::to_string(os::get_shell_process_id());
  if (name == "!")
    return m_last_background_pid ? std::to_string(*m_last_background_pid)
                                 : std::string{};
  if (name == "-") return option_flags_string();
  if (name == "#") return std::to_string(m_positional_params.size());
  if (name == "0") return m_shell_name;

  /* A purely numeric name selects a positional parameter, $1 upward. */
  if (!name.empty() &&
      std::all_of(name.begin(), name.end(),
                  [](unsigned char c) { return std::isdigit(c) != 0; }))
  {
    usize index = std::stoul(name);
    if (index >= 1 && index <= m_positional_params.size())
      return m_positional_params[index - 1];
    return std::string{};
  }

  /* $* and $@ outside the special quoted handling join into a single word. $*
     joins with the first IFS character, $@ joins with a space. */
  if (name == "*" || name == "@") {
    std::string separator = " ";
    if (name == "*") {
      std::string ifs = get_variable_value("IFS").value_or(" \t\n");
      separator = ifs.empty() ? std::string{} : std::string{ifs.front()};
    }
    std::string joined{};
    for (usize i = 0; i < m_positional_params.size(); i++) {
      if (i > 0) joined += separator;
      joined += m_positional_params[i];
    }
    return joined;
  }

  if (auto it = m_shell_variables.find(name); it != m_shell_variables.end())
    return it->second;

  return os::get_environment_variable(name);
}

const std::vector<std::string> &
EvalContext::positional_params() const
{
  return m_positional_params;
}

void
EvalContext::set_positional_params(std::vector<std::string> params)
{
  m_positional_params = std::move(params);
}

void
EvalContext::set_last_background_pid(i64 pid)
{
  m_last_background_pid = pid;
}

void
EvalContext::register_function(const std::string &name, const Expression *body)
{
  m_functions[name] = body;
}

const Expression *
EvalContext::find_function(const std::string &name) const
{
  if (auto it = m_functions.find(name); it != m_functions.end())
    return it->second;
  return nullptr;
}

void
EvalContext::clear_functions()
{
  m_functions.clear();
}

EvalStateSnapshot
EvalContext::snapshot_state() const
{
  return EvalStateSnapshot{m_shell_variables, m_positional_params,
                           m_last_exit_status, utils::get_current_directory()};
}

void
EvalContext::restore_state(EvalStateSnapshot snapshot)
{
  m_shell_variables = std::move(snapshot.shell_variables);
  m_positional_params = std::move(snapshot.positional_params);
  m_last_exit_status = snapshot.last_exit_status;
  utils::set_current_directory(snapshot.working_directory);
}

std::string
EvalContext::option_flags_string() const
{
  std::string flags{};
  if (m_error_exit) flags += 'e';
  if (!m_enable_path_expansion) flags += 'f';
  if (m_enable_echo) flags += 'v';
  if (m_enable_echo_expanded) flags += 'x';
  if (m_shell_is_interactive) flags += 'i';
  return flags;
}

void
EvalContext::set_last_exit_status(i32 status)
{
  m_last_exit_status = status;
}

i32
EvalContext::last_exit_status() const
{
  return m_last_exit_status;
}

std::string
EvalContext::expand_variable(const std::string &name) const
{
  return get_variable_value(name).value_or("");
}

std::string
EvalContext::make_stats_string() const
{
  std::string s{};

  s += "[Stats\n";

  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Expansions: " + std::to_string(last_expansion_count());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Nodes evaluated: " + std::to_string(last_expressions_executed());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Total expansions: " + std::to_string(total_expansion_count());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Total nodes evaluated: " + std::to_string(total_expressions_executed());
  s += '\n';

  s += "]";

  return s;
}

bool
EvalContext::should_echo() const
{
  return m_enable_echo;
}

bool
EvalContext::should_echo_expanded() const
{
  return m_enable_echo_expanded;
}

bool
EvalContext::shell_is_interactive() const
{
  return m_shell_is_interactive;
}

usize
EvalContext::last_expressions_executed() const
{
  return m_expressions_executed_last;
}

usize
EvalContext::total_expressions_executed() const
{
  return m_expressions_executed_total + m_expressions_executed_last;
}

usize
EvalContext::last_expansion_count() const
{
  return m_expansions_last;
}

usize
EvalContext::total_expansion_count() const
{
  return m_expansions_total + m_expansions_last;
}

/* TODO: Test symlinks. */
/* TODO: What the fuck is happening. */
std::vector<GlobField>
EvalContext::expand_path_once(const GlobField &field, bool should_expand_files)
{
  std::vector<GlobField> expanded{};

  const std::string &path = field.text;

  usize last_slash = path.rfind('/');
  bool has_slashes = (last_slash != std::string::npos);

  /* Prefix is the parent directory. */
  std::string parent_dir{};
  if (has_slashes)
    parent_dir =
        (last_slash != 0) ? path.substr(0, last_slash) : path.substr(0, 1);
  else
    parent_dir = ".";

  /* Stem of the glob after the last slash. Its mask starts at stem_start in the
     field, so glob_matches reads field.glob_active from there. */
  usize stem_start = has_slashes ? last_slash + 1 : 0;
  bool has_glob = stem_start < path.length();
  std::string_view glob{};
  if (has_glob) glob = std::string_view{path}.substr(stem_start);

  std::filesystem::directory_iterator d{};
  try {
    d = std::filesystem::directory_iterator{parent_dir};
  } catch (const std::filesystem::filesystem_error &e) {
    SHIT_UNUSED(e);
    throw Error{"Could not descend into '" + parent_dir +
                "': " + os::last_system_error_message()};
  }

  if (!has_glob) {
    expanded.push_back(field);
    return expanded;
  }

  for (const std::filesystem::directory_entry &e : d) {
    if (!should_expand_files && !e.is_directory()) continue;

    std::string filename = e.path().filename().string();

    /* TODO: Figure the rules of hidden file expansion. */
    if (glob[0] != '.' && filename[0] == '.') continue;

    if (utils::glob_matches(glob, filename, field.glob_active, stem_start)) {
      std::string expanded_path{};

      if (parent_dir != ".") {
        expanded_path += parent_dir;
        if (parent_dir != "/") expanded_path += '/';
      }
      expanded_path += filename;

      add_expansion();

      /* A real filename is literal, so the resulting field never globs again.
       */
      GlobField result_field{};
      result_field.glob_active.assign(expanded_path.length(), false);
      result_field.text = std::move(expanded_path);
      expanded.push_back(std::move(result_field));
    }
  }

  return expanded;
}

std::vector<GlobField>
EvalContext::expand_path_recurse(const std::vector<GlobField> &fields)
{
  std::vector<GlobField> result{};

  for (const GlobField &field : fields) {
    const std::string &text = field.text;

    std::optional<usize> expand_ch{};
    for (usize j = 0; j < text.length(); j++) {
      if (field.glob_active[j] && lexer::is_expandable_char(text[j])) {
        expand_ch = j;
        break;
      }
    }

    if (!expand_ch) {
      result.push_back(field);
      continue;
    }

    std::optional<usize> slash_after{};
    for (usize k = *expand_ch; k < text.length(); k++) {
      if (text[k] == '/') {
        slash_after = k;
        break;
      }
    }

    GlobField operating = field;
    GlobField removed_suffix{};
    if (slash_after) {
      std::ptrdiff_t slash_offset = static_cast<std::ptrdiff_t>(*slash_after);
      removed_suffix.text = text.substr(*slash_after);
      removed_suffix.glob_active.assign(
          field.glob_active.begin() + slash_offset, field.glob_active.end());
      operating.text = text.substr(0, *slash_after);
      operating.glob_active.assign(field.glob_active.begin(),
                                   field.glob_active.begin() + slash_offset);
    }

    std::vector<GlobField> once =
        expand_path_once(operating, !slash_after.has_value());

    if (!slash_after) {
      for (GlobField &f : once)
        result.emplace_back(std::move(f));
      continue;
    }

    /* Bring back the removed suffix and recurse on the expanded entries. */
    for (GlobField &f : once) {
      f.text += removed_suffix.text;
      f.glob_active.insert(f.glob_active.end(),
                           removed_suffix.glob_active.begin(),
                           removed_suffix.glob_active.end());
    }

    std::vector<GlobField> twice = expand_path_recurse(once);
    for (GlobField &f : twice) {
      try {
        /* FIXME: This is a massive slowdown. */
        if (!std::filesystem::exists(f.text)) continue;
      } catch (const std::filesystem::filesystem_error &e) {
        throw Error{"Could not check whether '" + e.path1().string() +
                    "' exists: " + os::last_system_error_message()};
      }
      result.emplace_back(std::move(f));
    }
  }

  return result;
}

void
EvalContext::expand_tilde(WordSegment &leading_segment) const
{
  /* A tilde only expands when it is unquoted. An escaped or quoted tilde is a
     literal segment and stays as is. */
  if (!leading_segment.is_tilde_candidate()) return;

  std::string &text = leading_segment.text;
  if (text.empty() || text[0] != '~') return;

  /* TODO: There may be several separators supported. */
  /* Only a bare ~ or a ~/ prefix expands. ~user is left alone for now. */
  if (text.length() > 1 && text[1] != '/') return;

  std::optional<std::filesystem::path> home = os::get_home_directory();
  if (!home) throw Error{"Could not figure out home directory"};

  text.erase(0, 1);
  text.insert(0, home->string());
}

std::vector<std::string>
EvalContext::expand_path(GlobField field)
{
  /* Fast path. A field with no live glob metacharacter is its own single
     result, so it skips the recursion and every copy. */
  bool has_glob = false;
  if (m_enable_path_expansion) {
    for (usize i = 0; i < field.text.length(); i++) {
      if (field.glob_active[i] && lexer::is_expandable_char(field.text[i])) {
        has_glob = true;
        break;
      }
    }
  }

  if (!has_glob) {
    std::vector<std::string> single{};
    single.emplace_back(std::move(field.text));
    return single;
  }

  /* The pattern is kept for the error, since the field moves into the recurse.
   */
  std::string pattern = field.text;

  std::vector<GlobField> fields = expand_path_recurse({std::move(field)});
  std::vector<std::string> values{};
  values.reserve(fields.size());
  for (GlobField &f : fields)
    values.emplace_back(std::move(f.text));

  /* Sort expansion in lexicographical order. Ignore punctuation to be somewhat
     compatible with bash. */
  std::stable_sort(
      values.begin(), values.end(), [](const auto &lhs, const auto &rhs) {
        const auto x =
            mismatch(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(),
                     [](const auto &lhs_ch, const auto &rhs_ch) {
                       return tolower(static_cast<unsigned char>(lhs_ch)) ==
                                  tolower(static_cast<unsigned char>(rhs_ch)) ||
                              (ispunct(static_cast<unsigned char>(lhs_ch)) &&
                               ispunct(static_cast<unsigned char>(rhs_ch)));
                     });
        return x.second != rhs.cend() &&
               (x.first == lhs.cend() ||
                tolower(static_cast<unsigned char>(*x.first)) <
                    tolower(static_cast<unsigned char>(*x.second)));
      });

  /* Error out on bogus expansion. */
  if (values.empty()) throw Error{"No expansions found for '" + pattern + "'"};

  return values;
}

std::vector<GlobField>
EvalContext::expand_word(const Word &word) const
{
  /* Only copy the segments when a leading tilde must be rewritten. The common
     word has no tilde and reads its segments in place. */
  const std::vector<WordSegment> *segments = &word.segments;
  std::vector<WordSegment> tilde_expanded_segments;
  if (!word.segments.empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.empty() &&
      word.segments.front().text.front() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front());
    segments = &tilde_expanded_segments;
  }

  /* The field separator defaults to whitespace when IFS is unset. */
  std::string ifs = get_variable_value("IFS").value_or(" \t\n");

  std::vector<GlobField> fields{};
  GlobField current{};
  bool has_current = false;

  auto flush = [&fields, &current, &has_current]() {
    if (has_current) {
      fields.emplace_back(std::move(current));
      current = GlobField{};
      has_current = false;
    }
  };

  auto append_run = [&current, &has_current](const std::string &text,
                                             bool glob_active) {
    current.text += text;
    current.glob_active.insert(current.glob_active.end(), text.length(),
                               glob_active);
    has_current = true;
  };

  /* A split run breaks into fields on every IFS run. Leading and trailing IFS
     leave no empty field behind, since flush only emits a started field. */
  auto append_split_run = [&](const std::string &text, bool glob_active) {
    usize i = 0;
    while (i < text.length()) {
      if (ifs.find(text[i]) != std::string::npos) {
        flush();
        while (i < text.length() && ifs.find(text[i]) != std::string::npos)
          i++;
        continue;
      }
      usize start = i;
      while (i < text.length() && ifs.find(text[i]) == std::string::npos)
        i++;
      append_run(text.substr(start, i - start), glob_active);
    }
  };

  for (const WordSegment &segment : *segments) {
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText:
    case WordSegment::Kind::DoubleQuotedText:
      append_run(segment.text, false);
      break;
    case WordSegment::Kind::UnquotedText:
      append_split_run(segment.text, true);
      break;
    case WordSegment::Kind::VariableReference: {
      /* "$@" expands to one field per positional parameter. The first joins any
         preceding text, the last leaves its field open for following text. */
      if (segment.text == "@" && segment.is_in_double_quotes) {
        for (usize i = 0; i < m_positional_params.size(); i++) {
          if (i > 0) flush();
          append_run(m_positional_params[i], false);
        }
        break;
      }
      std::string value = expand_variable(segment.text);
      if (segment.is_in_double_quotes)
        append_run(value, false);
      else
        append_split_run(value, false);
    } break;
    }
  }

  flush();

  return fields;
}

std::string
EvalContext::expand_word_for_assignment(const Word &word) const
{
  std::vector<WordSegment> segments = word.segments;
  if (!segments.empty()) expand_tilde(segments[0]);

  std::string result{};
  for (const WordSegment &segment : segments) {
    if (segment.kind == WordSegment::Kind::VariableReference)
      result += expand_variable(segment.text);
    else
      result += segment.text;
  }
  return result;
}

/* TODO: Command substitution. */
std::vector<std::string>
EvalContext::process_args(const std::vector<const Token *> &args)
{
  std::vector<std::string> expanded_args{};
  expanded_args.reserve(args.size());

  for (const Token *t : args) {
    SourceLocation l = t->source_location();
    try {
      /* A word token is expanded in place. Any other token is wrapped as one
         unquoted literal word, which is the only case that needs a temporary.
       */
      Word fallback_word{};
      const Word *word = nullptr;
      if (t->kind() == Token::Kind::Word) {
        word = &static_cast<const tokens::WordToken *>(t)->word();
      } else if (t->kind() == Token::Kind::Assignment) {
        /* An assignment that appears as an argument, like echo k=$v, is an
           ordinary word. Rebuild it as the literal key, an equals sign, and the
           value segments, so the value still expands instead of staying
           literal. */
        const tokens::Assignment *a =
            static_cast<const tokens::Assignment *>(t);
        fallback_word.segments.push_back(
            WordSegment{WordSegment::Kind::LiteralText, a->key() + "=", false});
        const Word &value = a->value_word();
        fallback_word.segments.insert(fallback_word.segments.end(),
                                      value.segments.begin(),
                                      value.segments.end());
        word = &fallback_word;
      } else {
        fallback_word.segments.push_back(WordSegment{
            WordSegment::Kind::UnquotedText, t->raw_string(), false});
        word = &fallback_word;
      }

      for (GlobField &field : expand_word(*word)) {
        for (std::string &g : expand_path(std::move(field)))
          expanded_args.emplace_back(std::move(g));
      }
    } catch (const Error &e) {
      throw ErrorWithLocation{l, e.message()};
    }
  }

  if (should_echo_expanded())
    std::cout << "+ " << utils::merge_args_to_string(expanded_args)
              << std::endl;

  return expanded_args;
}

/* clang-format off */
ExecContext::ExecContext(
    SourceLocation location,
    std::variant<shit::Builtin::Kind, std::filesystem::path> &&kind,
    const std::vector<std::string> &args)
    : m_kind(kind), m_location(location), m_args(args)
{}
/* clang-format on */

const SourceLocation &
ExecContext::source_location() const
{
  return m_location;
}

const std::string &
ExecContext::program() const
{
  return m_args[0];
}

const std::vector<std::string> &
ExecContext::args() const
{
  return m_args;
}

bool
ExecContext::is_builtin() const
{
  return std::holds_alternative<shit::Builtin::Kind>(m_kind);
}

const std::filesystem::path &
ExecContext::program_path() const
{
  SHIT_ASSERT(!is_builtin());
  return std::get<std::filesystem::path>(m_kind);
}

void
ExecContext::close_fds()
{
  if (in_fd) {
    os::close_fd(*in_fd);
    in_fd.reset();
  }
  if (out_fd) {
    os::close_fd(*out_fd);
    out_fd.reset();
  }
}

const Builtin::Kind &
ExecContext::builtin_kind() const
{
  SHIT_ASSERT(is_builtin());
  return std::get<shit::Builtin::Kind>(m_kind);
}

void
ExecContext::print_to_stdout(const std::string &s) const
{
  if (!os::write_fd(out_fd.value_or(SHIT_STDOUT), s.data(), s.size())
           .has_value())
  {
    throw Error{"Unable to write to stdout: " +
                os::last_system_error_message()};
  }
}

ExecContext
ExecContext::make_from(SourceLocation location,
                       const std::vector<std::string> &args)
{
  /* Make sure we always include at least one argument, the program path. */
  SHIT_ASSERT(args.size() > 0);

  std::variant<shit::Builtin::Kind, std::filesystem::path> kind;

  const std::string &program = args[0];

  std::optional<Builtin::Kind> bk;
  std::optional<std::filesystem::path> p;

  /* This isn't a path? */
  if (program.find('/') == std::string::npos) {
    bk = search_builtin(program);

    if (!bk) {
      /* Not a builtin, try to search PATH. */
      std::list<std::filesystem::path> ps = utils::search_program_path(program);
      if (ps.size() > 0) p = ps.front();
    }
  } else {
    /* This is a path. */
    /* TODO: Sanitize extensions here too. */
    p = utils::canonicalize_path(program);
  }

  /* Builtins take precedence over programs. */
  if (!bk) {
    if (p.has_value()) {
      kind = *p;
    } else {
      throw ErrorWithLocation{location,
                              "Program '" + program + "' wasn't found"};
    }
  } else {
    kind = *bk;
  }

  return {location, std::move(kind), args};
}

SourceLocation::SourceLocation(usize position, usize length)
    : m_position(position), m_length(length)
{}

usize
SourceLocation::position() const
{
  return m_position;
}

usize
SourceLocation::length() const
{
  return m_length;
}

void
SourceLocation::add_length(usize n)
{
  m_length += n;
}

} /* namespace shit */
