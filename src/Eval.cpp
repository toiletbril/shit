#include "Eval.hpp"

#include "Arena.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <thread>

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

  /* A purely numeric name selects a positional parameter, $1 upward. An index
     too large to fit, or beyond the count, has no value. */
  if (!name.empty() &&
      std::all_of(name.begin(), name.end(),
                  [](unsigned char c) { return std::isdigit(c) != 0; }))
  {
    if (name.size() > 9) return std::string{};
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

bool
EvalContext::has_functions() const
{
  return !m_functions.empty();
}

void
EvalContext::unset_function(const std::string &name)
{
  m_functions.erase(name);
}

std::unordered_set<std::string>
EvalContext::function_names() const
{
  std::unordered_set<std::string> names{};
  for (const auto &[name, body] : m_functions)
    names.insert(name);
  return names;
}

void
EvalContext::enter_subshell()
{
  m_subshell_depth++;
}

void
EvalContext::leave_subshell()
{
  m_subshell_depth--;
}

bool
EvalContext::in_subshell() const
{
  return m_subshell_depth > 0;
}

void
EvalContext::set_error_exit(bool enabled)
{
  m_error_exit = enabled;
}

bool
EvalContext::error_exit() const
{
  return m_error_exit;
}

void
EvalContext::set_echo_expanded(bool enabled)
{
  m_enable_echo_expanded = enabled;
}

void
EvalContext::set_error_unset(bool enabled)
{
  m_error_unset = enabled;
}

bool
EvalContext::error_unset() const
{
  return m_error_unset;
}

void
EvalContext::enter_condition()
{
  m_condition_depth++;
}

void
EvalContext::leave_condition()
{
  m_condition_depth--;
}

bool
EvalContext::in_condition() const
{
  return m_condition_depth > 0;
}

std::vector<std::string>
EvalContext::sorted_variable_assignments() const
{
  std::vector<std::string> assignments{};
  assignments.reserve(m_shell_variables.size());
  for (const auto &[name, value] : m_shell_variables)
    assignments.push_back(name + "=" + value);
  std::sort(assignments.begin(), assignments.end());
  return assignments;
}

void
EvalContext::clear_functions()
{
  m_functions.clear();
}

EvalStateSnapshot
EvalContext::snapshot_state() const
{
  return EvalStateSnapshot{m_shell_variables, m_functions, m_positional_params,
                           utils::get_current_directory()};
}

void
EvalContext::restore_state(EvalStateSnapshot snapshot)
{
  m_shell_variables = std::move(snapshot.shell_variables);
  m_functions = std::move(snapshot.functions);
  m_positional_params = std::move(snapshot.positional_params);
  utils::set_current_directory(snapshot.working_directory);
  /* The exit status is intentionally not restored. A subshell and a command
     substitution propagate the status of their last command to the parent. */
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

namespace {

/* Remove the shortest or longest prefix of value that matches pattern as a
   glob, returning the remainder. */
std::string
trim_matching_prefix(const std::string &value, const std::string &pattern,
                     bool longest)
{
  std::vector<bool> active(pattern.length(), true);
  if (longest) {
    for (usize length = value.length();; length--) {
      if (utils::glob_matches(pattern, value.substr(0, length), active, 0))
        return value.substr(length);
      if (length == 0) break;
    }
  } else {
    for (usize length = 0; length <= value.length(); length++) {
      if (utils::glob_matches(pattern, value.substr(0, length), active, 0))
        return value.substr(length);
    }
  }
  return value;
}

/* Remove the shortest or longest suffix of value that matches pattern as a
   glob, returning the head. */
std::string
trim_matching_suffix(const std::string &value, const std::string &pattern,
                     bool longest)
{
  std::vector<bool> active(pattern.length(), true);
  if (longest) {
    for (usize start = 0; start <= value.length(); start++) {
      if (utils::glob_matches(pattern, value.substr(start), active, 0))
        return value.substr(0, start);
    }
  } else {
    for (usize start = value.length();; start--) {
      if (utils::glob_matches(pattern, value.substr(start), active, 0))
        return value.substr(0, start);
      if (start == 0) break;
    }
  }
  return value;
}

} /* namespace */

std::string
EvalContext::expand_modifier_word(const std::string &word)
{
  std::string out{};
  for (usize i = 0; i < word.length(); i++) {
    if (word[i] != '$') {
      out += word[i];
      continue;
    }
    if (i + 1 >= word.length()) {
      out += '$';
      break;
    }

    char next = word[i + 1];
    if (next == '{') {
      std::string inner{};
      usize j = i + 2;
      i32 depth = 1;
      while (j < word.length()) {
        if (word[j] == '{') {
          depth++;
        } else if (word[j] == '}') {
          depth--;
          if (depth == 0) break;
        }
        inner += word[j];
        j++;
      }
      out += apply_parameter_expansion(inner);
      i = j;
    } else if (lexer::is_variable_name_start(next)) {
      std::string name{};
      usize j = i + 1;
      while (j < word.length() && lexer::is_variable_name(word[j]))
        name += word[j++];
      out += expand_variable(name);
      i = j - 1;
    } else if (next == '(' && i + 2 < word.length() && word[i + 2] == '(') {
      /* Arithmetic $((...)), scanned to the matching )). */
      std::string inner{};
      usize j = i + 3;
      usize depth = 0;
      for (; j < word.length(); j++) {
        if (word[j] == '(') {
          depth++;
        } else if (word[j] == ')' && depth > 0) {
          depth--;
        } else if (word[j] == ')' && j + 1 < word.length() &&
                   word[j + 1] == ')') {
          j += 2;
          break;
        }
        inner += word[j];
      }
      out += std::to_string(evaluate_arithmetic(inner));
      i = j - 1;
    } else if (next == '(') {
      /* Command substitution $(...), scanned to the matching ). */
      std::string inner{};
      usize j = i + 2;
      usize depth = 1;
      for (; j < word.length(); j++) {
        if (word[j] == '(') {
          depth++;
        } else if (word[j] == ')') {
          depth--;
          if (depth == 0) break;
        }
        inner += word[j];
      }
      out += capture_command_substitution(inner);
      i = j;
    } else if (next == '?' || next == '@' || next == '*' || next == '#' ||
               next == '$' || next == '!' || next == '-' ||
               lexer::is_number(next))
    {
      out += expand_variable(std::string{next});
      i++;
    } else {
      out += '$';
    }
  }
  return out;
}

std::string
EvalContext::apply_parameter_expansion(const std::string &spec)
{
  if (spec.empty()) return std::string{};

  /* ${#name} is the length of the value, distinct from $# which is the count of
     positional parameters. */
  if (spec.length() > 1 && spec[0] == '#') {
    std::string name = spec.substr(1);
    if (name == "@" || name == "*")
      return std::to_string(m_positional_params.size());
    std::optional<std::string> value = get_variable_value(name);
    if (m_error_unset && !value.has_value())
      throw Error{name + ": parameter not set"};
    return std::to_string(value.value_or("").length());
  }

  /* Split the parameter name from an optional operator and its word. */
  usize name_end = 0;
  if (lexer::is_variable_name_start(spec[0])) {
    while (name_end < spec.length() && lexer::is_variable_name(spec[name_end]))
      name_end++;
  } else if (lexer::is_number(spec[0])) {
    while (name_end < spec.length() && lexer::is_number(spec[name_end]))
      name_end++;
  } else {
    /* A special single-character parameter, such as ? or @. */
    name_end = 1;
  }

  std::string name = spec.substr(0, name_end);
  std::string rest = spec.substr(name_end);
  if (rest.empty()) {
    /* Under set -u a plain reference to a variable that is not set is an error,
       while a form with a modifier such as ${x:-w} handles the unset case
       itself. */
    if (m_error_unset && !get_variable_value(name).has_value())
      throw Error{name + ": parameter not set"};
    return expand_variable(name);
  }

  /* A leading colon makes the default, assign, alternate, and error forms treat
     an empty value as unset. */
  bool is_colon_form = rest[0] == ':';
  usize op_index = is_colon_form ? 1 : 0;
  if (op_index >= rest.length()) return expand_variable(name);

  char op = rest[op_index];
  bool is_doubled = (op_index + 1 < rest.length() &&
                     rest[op_index + 1] == op && (op == '#' || op == '%'));
  std::string word = rest.substr(op_index + (is_doubled ? 2 : 1));

  std::optional<std::string> current = get_variable_value(name);
  bool is_set = current.has_value();
  bool is_empty = !is_set || current->empty();
  bool treat_as_unset = is_colon_form ? is_empty : !is_set;

  switch (op) {
  case '-':
    return treat_as_unset ? expand_modifier_word(word) : *current;
  case '=':
    if (treat_as_unset) {
      std::string assigned = expand_modifier_word(word);
      set_shell_variable(name, assigned);
      return assigned;
    }
    return *current;
  case '+':
    return treat_as_unset ? std::string{} : expand_modifier_word(word);
  case '?':
    if (treat_as_unset) {
      throw Error{word.empty() ? name + ": parameter not set or empty"
                               : expand_modifier_word(word)};
    }
    return *current;
  case '#':
    return trim_matching_prefix(current.value_or(""),
                                expand_modifier_word(word), is_doubled);
  case '%':
    return trim_matching_suffix(current.value_or(""),
                                expand_modifier_word(word), is_doubled);
  default:
    return expand_variable(name);
  }
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

    /* The entry already holds the full path it built. On POSIX native() is a
       reference, so the filename is a view into it with no allocation. */
    const std::string &full = e.path().native();
    usize slash = full.rfind('/');
    std::string_view filename = (slash == std::string::npos)
                                    ? std::string_view{full}
                                    : std::string_view{full}.substr(slash + 1);

    /* TODO: Figure the rules of hidden file expansion. */
    if (glob[0] != '.' && !filename.empty() && filename[0] == '.') continue;

    if (utils::glob_matches(glob, filename, field.glob_active, stem_start)) {
      add_expansion();

      /* A real filename is literal, so the resulting field never globs again.
         The empty mask is the all-literal convention, so it carries no
         per-result allocation. The iterator built the full path already, so
         reuse it instead of joining the parent and filename again. A parent of
         "." is dropped to keep the result relative. */
      GlobField result_field{};
      if (parent_dir == ".")
        result_field.text = std::string{filename};
      else
        result_field.text = full;
      expanded.push_back(std::move(result_field));
    }
  }

  return expanded;
}

std::vector<GlobField>
EvalContext::expand_path_recurse(const std::vector<GlobField> &fields)
{
  std::vector<GlobField> result{};
  result.reserve(fields.size());

  for (const GlobField &field : fields) {
    const std::string &text = field.text;

    /* An empty mask is the all-literal convention, so a field without one holds
       no live glob metacharacter. */
    std::optional<usize> expand_ch{};
    for (usize j = 0; j < field.glob_active.size(); j++) {
      if (field.glob_active[j] && lexer::is_expandable_char(text[j])) {
        expand_ch = j;
        break;
      }
    }

    if (!expand_ch) {
      /* No glob remains. This field is a literal suffix appended after an
         earlier glob, so keep it only when it actually exists. A path produced
         purely by globbing came from a directory read and always exists, so it
         never reaches here and pays no stat. */
      std::error_code exists_error{};
      if (std::filesystem::exists(field.text, exists_error))
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

    /* The glob is the last component, so expand it against files and emit the
       matches as is. The field passes by reference with no copy. */
    if (!slash_after) {
      std::vector<GlobField> once = expand_path_once(field, true);
      for (GlobField &f : once)
        result.emplace_back(std::move(f));
      continue;
    }

    /* Split off the first globbed directory component and the literal-or-glob
       suffix after it, building each from a substring rather than copying the
       whole field. */
    std::ptrdiff_t slash_offset = static_cast<std::ptrdiff_t>(*slash_after);
    GlobField operating{};
    operating.text = text.substr(0, *slash_after);
    operating.glob_active.assign(field.glob_active.begin(),
                                 field.glob_active.begin() + slash_offset);
    GlobField removed_suffix{};
    removed_suffix.text = text.substr(*slash_after);
    removed_suffix.glob_active.assign(field.glob_active.begin() + slash_offset,
                                      field.glob_active.end());

    std::vector<GlobField> once = expand_path_once(operating, false);

    /* Bring back the removed suffix and recurse on the expanded entries. Each
       match came back all-literal with an empty mask, so restore its false
       entries before the suffix mask to keep the mask aligned with the text. */
    for (GlobField &f : once) {
      usize matched_length = f.text.length();
      f.text += removed_suffix.text;
      f.glob_active.assign(matched_length, false);
      f.glob_active.insert(f.glob_active.end(),
                           removed_suffix.glob_active.begin(),
                           removed_suffix.glob_active.end());
    }

    /* The recurse validates each level through the directory read or, for a
       literal suffix, the existence check above, so no extra stat is needed
       here. */
    std::vector<GlobField> twice = expand_path_recurse(once);
    for (GlobField &f : twice)
      result.emplace_back(std::move(f));
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
    for (usize i = 0; i < field.glob_active.size(); i++) {
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

  /* The pattern is kept so a glob that matches nothing falls back to it, since
     the field moves into the recurse. */
  std::string pattern = field.text;

  std::vector<GlobField> fields = expand_path_recurse({std::move(field)});
  std::vector<std::string> values{};
  values.reserve(fields.size());
  for (GlobField &f : fields)
    values.emplace_back(std::move(f.text));

  /* Sort the matches in byte order, which is the POSIX collating order in the C
     locale and what dash produces. A plain compare also keeps a large expansion
     from spending most of its time in the sort comparator. */
  std::sort(values.begin(), values.end());

  /* A pattern that matches no file expands to itself, the POSIX behavior dash
     follows, rather than being dropped or raising an error. */
  if (values.empty()) values.emplace_back(std::move(pattern));

  return values;
}

namespace {

/* A recursive-descent evaluator for $((...)), following C operator precedence,
   that resolves and assigns shell variables through the context. */
struct ArithmeticParser
{
  EvalContext &context;
  const std::string &source;
  usize pos;

  [[noreturn]] void fail(const std::string &message)
  {
    throw Error{"Arithmetic: " + message};
  }

  void skip_spaces()
  {
    while (pos < source.length() &&
           (source[pos] == ' ' || source[pos] == '\t' || source[pos] == '\n' ||
            source[pos] == '\r'))
      pos++;
  }

  bool starts_with(std::string_view op)
  {
    skip_spaces();
    return pos + op.size() <= source.length() &&
           std::string_view{source}.substr(pos, op.size()) == op;
  }

  bool consume(std::string_view op)
  {
    if (!starts_with(op)) return false;
    pos += op.size();
    return true;
  }

  i64 read_variable_value(const std::string &name)
  {
    std::string value = context.get_variable_value(name).value_or("");
    if (value.empty()) return 0;
    try {
      return std::stoll(value, nullptr, 0);
    } catch (...) {
      return 0;
    }
  }

  i64 parse()
  {
    i64 result = parse_assignment();
    skip_spaces();
    if (pos != source.length()) fail("unexpected trailing characters");
    return result;
  }

  i64 apply_compound(i64 lhs, i64 rhs, char kind)
  {
    switch (kind) {
    case '+': return lhs + rhs;
    case '-': return lhs - rhs;
    case '*': return lhs * rhs;
    case '/':
      if (rhs == 0) fail("division by zero");
      return lhs / rhs;
    case '%':
      if (rhs == 0) fail("division by zero");
      return lhs % rhs;
    case '&': return lhs & rhs;
    case '|': return lhs | rhs;
    case '^': return lhs ^ rhs;
    case 'L': return lhs << rhs;
    case 'R': return lhs >> rhs;
    default: return rhs;
    }
  }

  i64 parse_assignment()
  {
    /* An assignment has a bare variable name on the left, so try it and rewind
       when the name is not followed by an assignment operator. */
    usize save = pos;
    skip_spaces();
    if (pos < source.length() && lexer::is_variable_name_start(source[pos])) {
      std::string name{};
      while (pos < source.length() && lexer::is_variable_name(source[pos]))
        name += source[pos++];

      static const std::pair<std::string_view, char> compound_operators[] = {
          {"<<=", 'L'}, {">>=", 'R'}, {"+=", '+'}, {"-=", '-'},
          {"*=", '*'},  {"/=", '/'},  {"%=", '%'}, {"&=", '&'},
          {"|=", '|'},  {"^=", '^'},
      };
      for (const auto &[op, kind] : compound_operators) {
        if (consume(op)) {
          i64 rhs = parse_assignment();
          i64 result = apply_compound(read_variable_value(name), rhs, kind);
          context.set_shell_variable(name, std::to_string(result));
          return result;
        }
      }
      if (starts_with("=") && !starts_with("==")) {
        consume("=");
        i64 rhs = parse_assignment();
        context.set_shell_variable(name, std::to_string(rhs));
        return rhs;
      }
      pos = save;
    }
    return parse_ternary();
  }

  i64 parse_ternary()
  {
    i64 condition = parse_logical_or();
    if (consume("?")) {
      i64 if_true = parse_assignment();
      if (!consume(":")) fail("expected ':' in a conditional");
      i64 if_false = parse_ternary();
      return condition != 0 ? if_true : if_false;
    }
    return condition;
  }

  i64 parse_logical_or()
  {
    i64 lhs = parse_logical_and();
    while (consume("||"))
      lhs = (lhs != 0 || parse_logical_and() != 0) ? 1 : 0;
    return lhs;
  }

  i64 parse_logical_and()
  {
    i64 lhs = parse_bitwise_or();
    while (consume("&&"))
      lhs = (lhs != 0 && parse_bitwise_or() != 0) ? 1 : 0;
    return lhs;
  }

  i64 parse_bitwise_or()
  {
    i64 lhs = parse_bitwise_xor();
    while (starts_with("|") && !starts_with("||")) {
      consume("|");
      lhs |= parse_bitwise_xor();
    }
    return lhs;
  }

  i64 parse_bitwise_xor()
  {
    i64 lhs = parse_bitwise_and();
    while (consume("^"))
      lhs ^= parse_bitwise_and();
    return lhs;
  }

  i64 parse_bitwise_and()
  {
    i64 lhs = parse_equality();
    while (starts_with("&") && !starts_with("&&")) {
      consume("&");
      lhs &= parse_equality();
    }
    return lhs;
  }

  i64 parse_equality()
  {
    i64 lhs = parse_relational();
    for (;;) {
      if (consume("=="))
        lhs = (lhs == parse_relational()) ? 1 : 0;
      else if (consume("!="))
        lhs = (lhs != parse_relational()) ? 1 : 0;
      else
        break;
    }
    return lhs;
  }

  i64 parse_relational()
  {
    i64 lhs = parse_shift();
    for (;;) {
      if (consume("<="))
        lhs = (lhs <= parse_shift()) ? 1 : 0;
      else if (consume(">="))
        lhs = (lhs >= parse_shift()) ? 1 : 0;
      else if (starts_with("<") && !starts_with("<<")) {
        consume("<");
        lhs = (lhs < parse_shift()) ? 1 : 0;
      } else if (starts_with(">") && !starts_with(">>")) {
        consume(">");
        lhs = (lhs > parse_shift()) ? 1 : 0;
      } else
        break;
    }
    return lhs;
  }

  i64 parse_shift()
  {
    i64 lhs = parse_additive();
    for (;;) {
      if (consume("<<"))
        lhs <<= parse_additive();
      else if (consume(">>"))
        lhs >>= parse_additive();
      else
        break;
    }
    return lhs;
  }

  i64 parse_additive()
  {
    i64 lhs = parse_multiplicative();
    for (;;) {
      if (consume("+"))
        lhs += parse_multiplicative();
      else if (consume("-"))
        lhs -= parse_multiplicative();
      else
        break;
    }
    return lhs;
  }

  i64 parse_multiplicative()
  {
    i64 lhs = parse_unary();
    for (;;) {
      if (consume("*"))
        lhs *= parse_unary();
      else if (consume("/")) {
        i64 divisor = parse_unary();
        if (divisor == 0) fail("division by zero");
        lhs /= divisor;
      } else if (consume("%")) {
        i64 divisor = parse_unary();
        if (divisor == 0) fail("division by zero");
        lhs %= divisor;
      } else
        break;
    }
    return lhs;
  }

  i64 parse_unary()
  {
    if (consume("!")) return parse_unary() == 0 ? 1 : 0;
    if (consume("~")) return ~parse_unary();
    if (consume("-")) return -parse_unary();
    if (consume("+")) return parse_unary();
    return parse_primary();
  }

  i64 parse_primary()
  {
    skip_spaces();
    if (consume("(")) {
      i64 value = parse_assignment();
      if (!consume(")")) fail("expected ')'");
      return value;
    }
    if (pos < source.length() && lexer::is_number(source[pos])) {
      usize consumed = 0;
      i64 value = std::stoll(source.substr(pos), &consumed, 0);
      pos += consumed;
      return value;
    }
    if (pos < source.length() && lexer::is_variable_name_start(source[pos])) {
      std::string name{};
      while (pos < source.length() && lexer::is_variable_name(source[pos]))
        name += source[pos++];
      return read_variable_value(name);
    }
    fail("unexpected character");
  }
};

} /* namespace */

i64
EvalContext::evaluate_arithmetic(const std::string &expression)
{
  /* Parameter expansion runs first, so a $1, a $x, or a ${...} inside the
     arithmetic becomes its value before the expression is parsed. A bare name
     is still resolved during evaluation. */
  std::string expanded = expand_modifier_word(expression);
  ArithmeticParser parser{*this, expanded, 0};
  return parser.parse();
}

std::vector<GlobField>
EvalContext::expand_word(const Word &word)
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
      std::string value = apply_parameter_expansion(segment.text);
      if (segment.is_in_double_quotes)
        append_run(value, false);
      else
        append_split_run(value, false);
    } break;
    case WordSegment::Kind::CommandSubstitution: {
      std::string output = capture_command_substitution(segment.text);
      if (segment.is_in_double_quotes)
        append_run(output, false);
      else
        append_split_run(output, false);
    } break;
    case WordSegment::Kind::ArithmeticExpansion: {
      std::string value = std::to_string(evaluate_arithmetic(segment.text));
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
EvalContext::expand_word_for_assignment(const Word &word)
{
  std::vector<WordSegment> segments = word.segments;
  if (!segments.empty()) expand_tilde(segments[0]);

  std::string result{};
  for (const WordSegment &segment : segments) {
    if (segment.kind == WordSegment::Kind::VariableReference)
      result += apply_parameter_expansion(segment.text);
    else if (segment.kind == WordSegment::Kind::CommandSubstitution)
      result += capture_command_substitution(segment.text);
    else if (segment.kind == WordSegment::Kind::ArithmeticExpansion)
      result += std::to_string(evaluate_arithmetic(segment.text));
    else
      result += segment.text;
  }
  return result;
}

std::string
EvalContext::capture_command_substitution(const std::string &source)
{
  /* Parse the inner command into the active parse arena. It coexists with the
     outer tree and is reclaimed when the arena resets. */
  if (g_ast_arena == nullptr)
    throw Error{"Command substitution outside of a parse"};

  Parser parser{
      Lexer{source, *g_ast_arena}
  };
  std::unique_ptr<Expression> ast = parser.construct_ast();

  /* A cd or an assignment inside the substitution must not leak. */
  EvalStateSnapshot snapshot = snapshot_state();

  std::optional<os::Pipe> pipe = os::make_pipe();
  if (!pipe) throw Error{"Could not open a pipe for command substitution"};

  /* Drain the read end on a thread so output larger than the pipe buffer cannot
     deadlock the commands writing into it. */
  std::string captured{};
  std::thread reader([&captured, read_fd = pipe->in]() {
    /* A failed allocation here must not escape the thread and call terminate.
     */
    try {
      char buffer[4096];
      for (;;) {
        std::optional<usize> n = os::read_fd(read_fd, buffer, sizeof(buffer));
        if (!n.has_value() || *n == 0) break;
        captured.append(buffer, *n);
      }
    } catch (...) {}
  });

  std::cout.flush();
  os::descriptor saved = os::redirect_stdout(pipe->out);

  /* The inner commands write to the pipe, not the terminal, so suppress the
     interactive title updates while the substitution runs. */
  bool was_interactive = m_shell_is_interactive;
  m_shell_is_interactive = false;

  /* Run the inner command, then always tear down, even on an error. A break,
     continue, return, or exit inside a substitution acts only within it and
     must not escape into the enclosing loop, function, or shell. */
  enter_subshell();
  std::exception_ptr error;
  try {
    ast->evaluate(*this);
  } catch (const ShellExit &exited) {
    set_last_exit_status(static_cast<i32>(exited.status));
  } catch (const LoopControl &) {
  } catch (const FunctionReturn &) {
  } catch (...) {
    error = std::current_exception();
  }
  leave_subshell();

  m_shell_is_interactive = was_interactive;

  std::cout.flush();
  os::restore_stdout(saved);
  os::close_fd(pipe->out);
  reader.join();
  os::close_fd(pipe->in);
  restore_state(std::move(snapshot));

  if (error) std::rethrow_exception(error);

  while (!captured.empty() && captured.back() == '\n')
    captured.pop_back();
  return captured;
}

i32
EvalContext::run_source(const std::string &source)
{
  /* Parse into the active arena, coexisting with the outer tree, the same way a
     command substitution does. The control-flow exceptions are not caught here,
     so a return or a break inside the evaluated source reaches the caller. */
  if (g_ast_arena == nullptr)
    throw Error{"Cannot run source outside of a parse"};

  Parser parser{
      Lexer{source, *g_ast_arena}
  };

  /* Retain the AST before evaluating, so a function it defines outlives this
     call and a control-flow exception thrown inside still leaves it owned. The
     destructor runs at the next top-level command, freeing the node members
     while the arena storage is reclaimed by the reset. */
  Expression *ast = parser.construct_ast().release();
  m_retained_source_asts.push_back(ast);
  ast->evaluate(*this);
  return last_exit_status();
}

void
EvalContext::clear_retained_sources()
{
  for (Expression *ast : m_retained_source_asts)
    delete ast;
  m_retained_source_asts.clear();
}

void
EvalContext::retain_ast(Expression *ast)
{
  m_retained_source_asts.push_back(ast);
}

std::string
EvalContext::expand_heredoc_body(const std::string &body)
{
  return expand_modifier_word(body);
}

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

  /* The trace goes to standard error, the way bash does it, so it stays out of
     a command substitution's captured output. The plus is repeated once per
     enclosing subshell, so the top shell shows '+', a substitution '++', and a
     nested one '+++'. */
  if (should_echo_expanded()) {
    std::string prefix(m_subshell_depth + 1, '+');
    std::cerr << prefix << ' ' << utils::merge_args_to_string(expanded_args)
              << '\n';
  }

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
  if (err_fd) {
    os::close_fd(*err_fd);
    err_fd.reset();
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

ExecContext
ExecContext::from_resolved(
    SourceLocation location,
    std::variant<shit::Builtin::Kind, std::filesystem::path> kind,
    const std::vector<std::string> &args)
{
  SHIT_ASSERT(args.size() > 0);
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
