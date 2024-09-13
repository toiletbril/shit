#include "Eval.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>

namespace shit {

/**
 * class: EvalContext
 */
EvalContext::EvalContext(bool should_disable_path_expansion)
    : m_enable_path_expansion(!should_disable_path_expansion)
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
  m_expansions_last = 0;

  m_expressions_executed_total += m_expressions_executed_last;
  m_expressions_executed_last = 0;
}

void
EvalContext::steal_escape_map(const EscapeMap &&em)
{
  m_escape_map = em;
}

std::string
EvalContext::make_stats_string() const
{
  std::string s{};

  s += "[Statistics:\n";

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
std::vector<std::string>
EvalContext::expand_path_once(std::string_view path, usize source_position,
                              bool should_expand_files)
{
  std::vector<std::string> expanded_paths{};

  usize last_slash = path.rfind('/');
  bool  has_slashes = (last_slash != std::string::npos);

  /* Prefix is the parent directory. */
  std::string parent_dir{};

  if (has_slashes) {
    if (last_slash != 0)
      parent_dir = path.substr(0, last_slash);
    else
      parent_dir = path.substr(0, 1);
  } else {
    parent_dir = ".";
  }

  /* Stem of the glob after the last slash. */
  std::optional<std::string_view> glob{};

  if (has_slashes) {
    if (last_slash + 1 < path.length()) {
      glob = path;
      glob->remove_prefix(last_slash + 1);
    } else {
      /* glob is empty. */
    }
  } else {
    glob = path;
  }

  std::filesystem::directory_iterator d{};

  try {
    d = std::filesystem::directory_iterator{parent_dir};
  } catch (std::filesystem::filesystem_error &e) {
    throw Error{"Could not descend into '" + std::string{parent_dir} +
                "': " + os::last_system_error_message()};
  }

  if (glob) {
    for (const std::filesystem::directory_entry &e : d) {
      if (!should_expand_files && !e.is_directory()) continue;
      std::string f = e.path().filename().string();

      /* TODO: Figure the rules of hidden file expansion. */
      if ((*glob)[0] != '.' && f[0] == '.') continue;

      if (utils::glob_matches(*glob, f, source_position, m_escape_map)) {
        std::string expanded_path{};
        if (parent_dir != ".") {
          expanded_path += parent_dir;
          if (parent_dir != "/") expanded_path += '/';
        }
        expanded_path += f;
        add_expansion();
        expanded_paths.emplace_back(expanded_path);
      }
    }
  } else {
    expanded_paths.emplace_back(path);
  }

  return expanded_paths;
}

std::vector<std::string>
EvalContext::expand_path_recurse(const std::vector<std::string> &paths,
                                 usize source_position)
{
  std::vector<std::string> resulting_expanded_paths{};
  std::optional<usize>     expand_ch{};

  for (std::string_view original_path : paths) {
    std::string_view path = original_path;

    for (usize i = 0; i < path.length(); i++) {
      if (lexer::is_expandable_char(path[i]) &&
          !m_escape_map.is_escaped(source_position + i))
      {
        expand_ch = i;
        break;
      }
    }
    if (expand_ch) {
      std::optional<usize> slash_after{};

      for (usize i = *expand_ch; i < path.length(); i++) {
        if (path[i] == '/') {
          slash_after = i;
          break;
        }
      }

      if (slash_after) path.remove_suffix(path.length() - *slash_after);

      std::vector<std::string> expanded_paths =
          expand_path_once(path, source_position, !slash_after);

      if (slash_after) {
        /* Bring back the removed suffix. */
        std::string_view removed_suffix = original_path;
        removed_suffix.remove_prefix(*slash_after);

        for (std::string &expanded_path : expanded_paths) {
          expanded_path += removed_suffix;
        }

        /* Call this function recursively on expanded entries. */
        std::vector<std::string> twice_expanded_paths =
            expand_path_recurse(expanded_paths, source_position);
        for (const std::string &twice_expanded_path : twice_expanded_paths) {
          resulting_expanded_paths.emplace_back(twice_expanded_path);
        }
      } else {
        for (const std::string &resulting_path : expanded_paths)
          resulting_expanded_paths.emplace_back(resulting_path);
      }
    } else {
      resulting_expanded_paths.emplace_back(path);
    }
  }

  return resulting_expanded_paths;
}

usize
EvalContext::expand_tilde(std::string &p, usize source_position)
{
  if (p[0] == '~' && !m_escape_map.is_escaped(source_position)) {
    /* TODO: There may be several separators supported. */
    if (p.length() > 1 && p[1] != '/') {
      /* TODO: Expand different users. */
      return 0;
    }

    /* Remove the tilde. */
    p.erase(0, 1);

    std::optional<std::filesystem::path> u = os::get_home_directory();
    if (!u) {
      throw Error{"Could not figure out home directory"};
    }

    std::string s{u->string()};

    p.insert(0, s);

    return s.size() - 1;
  }

  return 0;
}

std::vector<std::string>
EvalContext::expand_path(std::string &&r, usize source_position)
{
  std::vector<std::string> values{};

  if (m_enable_path_expansion) {
    values = expand_path_recurse({r}, source_position);
  } else {
    values.emplace_back(r);
  }

  /* Sort expansion in lexicographical order. Ignore punctuation to be somewhat
   * compatible with bash. */
  std::stable_sort(
      values.begin(), values.end(), [](const auto &lhs, const auto &rhs) {
        const auto x =
            mismatch(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(),
                     [](const auto &lhs, const auto &rhs) {
                       return tolower(lhs) == tolower(rhs) ||
                              (ispunct(lhs) && ispunct(rhs));
                     });
        return x.second != rhs.cend() &&
               (x.first == lhs.cend() ||
                tolower(*x.first) < tolower(*x.second));
      });

  if (values.empty()) {
    throw Error{"No expansions found for '" + r + "'"};
  }

  return values;
}

/* TODO: Command substitution. */
std::vector<std::string>
EvalContext::process_args(const std::vector<const Token *> &args)
{
  std::vector<std::string> expanded_args{};
  expanded_args.reserve(args.size());

  usize tilde_offset = 0;

  for (const Token *t : args) {
    try {
      std::string r = t->raw_string();
      tilde_offset += expand_tilde(r, t->source_location().position());
      std::vector<std::string> e = expand_path(
          std::move(r), t->source_location().position() - tilde_offset);
      for (std::string &a : e)
        expanded_args.emplace_back(a);
    } catch (Error &e) {
      throw ErrorWithLocation{t->source_location(),
                              "Could not expand path: " + e.message()};
    }
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
  if (!is_builtin()) {
    return std::get<std::filesystem::path>(m_kind);
  }

  throw shit::Error{"program_path() call on a builtin"};
}

void
ExecContext::close_fds()
{
  if (in_fd) os::close_fd(*in_fd);
  if (out_fd) os::close_fd(*out_fd);
}

const Builtin::Kind &
ExecContext::builtin_kind() const
{
  if (is_builtin()) {
    return std::get<shit::Builtin::Kind>(m_kind);
  }

  SHIT_UNREACHABLE("builtin_kind() call on a program");
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
ExecContext::make_from(SourceLocation                  location,
                       const std::vector<std::string> &args)
{
  SHIT_ASSERT(args.size() > 0);

  std::variant<shit::Builtin::Kind, std::filesystem::path> kind;

  const std::string &program = args[0];

  std::optional<Builtin::Kind>         bk;
  std::optional<std::filesystem::path> p;

  /* This isn't a path? */
  if (program.find('/') == std::string::npos) {
    bk = search_builtin(program);

    if (!bk) {
      /* Not a builtin, try to search PATH. */
      p = utils::search_program_path(program);
    }
  } else {
    /* This is a path. */
    /* TODO: Sanitize extensions here too. */
    p = utils::canonicalize_path(program);
  }

  /* Builtins take precedence over programs. */
  if (!bk) {
    if (p)
      kind = *p;
    else
      throw ErrorWithLocation{location,
                              "Program '" + program + "' wasn't found"};
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

EscapeMap::EscapeMap() : m_bitmap() {}

void
EscapeMap::add_escape(usize position)
{
  if (m_bitmap.size() * 8 <= position) m_bitmap.resize(position / 8 + 1);
  m_bitmap[position / 8] |= (1 << (position % 8));
}

bool
EscapeMap::is_escaped(usize position) const
{
  if (m_bitmap.size() * 8 <= position) return false;
  return m_bitmap[position / 8] & (1 << (position % 8));
}

std::string
EscapeMap::to_string() const
{
  std::string s{};
  for (usize byte = 0; byte < m_bitmap.size(); byte++) {
    for (usize bit = 0; bit < 8; bit++) {
      s += std::to_string((m_bitmap[byte] >> bit) & 1);
    }
    s += ' ';
  }
  return s;
}

} /* namespace shit */
