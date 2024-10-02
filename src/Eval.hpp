#pragma once

#include "Builtin.hpp"
#include "Common.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace shit {

struct SourceLocation
{
  SourceLocation() = delete;
  SourceLocation(usize position, usize length);

  /* Both variables are byte-offsets and do not account for unicode. */
  usize position() const;
  usize length() const;

private:
  usize m_position;
  usize m_length;
};

struct EscapeMap
{
  EscapeMap();

  void add_escape(usize position);
  bool is_escaped(usize position) const;

  std::string to_string() const;

private:
  std::vector<u8> m_bitmap;
};

struct Token;

struct EvalContext
{
  EvalContext(bool should_disable_path_expansion, bool should_echo,
              bool should_echo_expanded, bool shell_is_interactive);

  void add_expansion();
  void add_evaluated_expression();

  void end_command();

  const EscapeMap &escape_map() const;
  void             steal_escape_map(const EscapeMap &&em);

  /* Path-expand, tilde-expand and escape. */
  std::vector<std::string> process_args(const std::vector<const Token *> &args);

  bool should_echo() const;
  bool should_echo_expanded() const;
  bool shell_is_interactive() const;

  std::string make_stats_string() const;

  usize last_expressions_executed() const;
  usize total_expressions_executed() const;

  usize last_expansion_count() const;
  usize total_expansion_count() const;

protected:
  usize m_expressions_executed_last{0};
  usize m_expressions_executed_total{0};
  usize m_expansions_last{0};
  usize m_expansions_total{0};

  EscapeMap m_escape_map;
  bool      m_enable_path_expansion;

  bool m_enable_echo;
  bool m_enable_echo_expanded;
  bool m_shell_is_interactive;

  /* clang-format off */
  std::tuple<std::vector<std::string>, std::vector<usize>>
  expand_path_once(std::string_view r,
                   usize source_position,
                   usize offset,
                   bool should_count_files);
  std::vector<std::string>

  expand_path_recurse(const std::vector<std::string> &vs,
                      const std::vector<usize> &os,
                      usize source_position);
  std::vector<std::string>

  expand_path(std::string &&r, usize source_position);
  /* clang-format off */

  /* Returns an offset by which the string was shifted. */
  usize expand_tilde(std::string &r, usize source_position) const;

};

/* Lower-level execution context. Path is the program path to execute, expanded
 * from program. Program is non-altered first arg. */
struct ExecContext
{
  static ExecContext make_from(SourceLocation                  location,
                               const std::vector<std::string> &args);

  std::optional<os::descriptor> in_fd{std::nullopt};
  std::optional<os::descriptor> out_fd{std::nullopt};

  bool is_builtin() const;

  const std::vector<std::string> &args() const;
  const std::string              &program() const;
  const SourceLocation           &source_location() const;

  void close_fds();
  void print_to_stdout(const std::string &s) const;

  i32 execute(bool is_async);

  const std::filesystem::path &program_path() const;
  const Builtin::Kind         &builtin_kind() const;

private:
  /* clang-format off */
  ExecContext(SourceLocation location,
              std::variant<shit::Builtin::Kind, std::filesystem::path> &&kind,
              const std::vector<std::string> &args);
  /* clang-format on */

  std::variant<shit::Builtin::Kind, std::filesystem::path> m_kind;

  SourceLocation           m_location;
  std::vector<std::string> m_args;
};

} /* namespace shit */
