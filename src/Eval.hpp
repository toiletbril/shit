#pragma once

#include "Builtin.hpp"
#include "Common.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
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

  void add_length(usize n);

private:
  usize m_position;
  usize m_length;
};

/* A field is a candidate argument after variable expansion and field splitting.
   It carries a parallel mask that marks which characters may act as glob
   metacharacters, so globbing needs no source-position arithmetic. */
struct GlobField
{
  std::string text;
  std::vector<bool> glob_active;
};

struct Token;
struct Word;
struct WordSegment;
struct Expression;

/* The break and continue builtins throw this to unwind to the enclosing loop.
   The level counts how many loops to act on, so break 2 leaves two loops. */
struct LoopControl
{
  enum class Kind : u8
  {
    Break,
    Continue,
  };

  Kind kind;
  i64 level;
};

/* A snapshot of the mutable shell state, taken around a subshell or a command
   substitution so a cd or an assignment inside does not leak to the parent. */
struct EvalStateSnapshot
{
  std::unordered_map<std::string, std::string> shell_variables;
  std::vector<std::string> positional_params;
  i32 last_exit_status;
  std::filesystem::path working_directory;
};

struct EvalContext
{
  EvalContext(bool should_disable_path_expansion, bool should_echo,
              bool should_echo_expanded, bool shell_is_interactive,
              bool should_error_exit = false, std::string shell_name = {},
              std::vector<std::string> positional_params = {});

  void add_expansion();
  void add_evaluated_expression();

  void end_command();

  /* Variable expand, tilde expand, field split, and glob each token. */
  std::vector<std::string> process_args(const std::vector<const Token *> &args);

  void set_shell_variable(const std::string &name, std::string value);
  void unset_shell_variable(const std::string &name);
  std::optional<std::string> get_variable_value(const std::string &name) const;

  /* The positional parameters, $1 upward, with $0 separate as the shell name.
   */
  const std::vector<std::string> &positional_params() const;
  void set_positional_params(std::vector<std::string> params);

  void set_last_exit_status(i32 status);
  i32 last_exit_status() const;

  /* The process id of the most recent background command, for $!. */
  void set_last_background_pid(i64 pid);

  /* Shell functions live in the parse arena, so the table is cleared before
     each top-level parse to avoid pointing at freed storage. A function shadows
     a builtin and a program of the same name. */
  void register_function(const std::string &name, const Expression *body);
  const Expression *find_function(const std::string &name) const;
  void clear_functions();

  /* Save and restore the mutable state around a subshell or a command
     substitution, so changes inside do not leak to the parent. */
  EvalStateSnapshot snapshot_state() const;
  void restore_state(EvalStateSnapshot snapshot);

  /* Expand a word in assignment context, with variable and tilde expansion but
     no field splitting and no globbing. */
  std::string expand_word_for_assignment(const Word &word) const;

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

  std::unordered_map<std::string, std::string> m_shell_variables{};
  i32 m_last_exit_status{0};

  std::string m_shell_name{};
  std::vector<std::string> m_positional_params{};
  std::optional<i64> m_last_background_pid{};
  std::unordered_map<std::string, const Expression *> m_functions{};

  bool m_enable_path_expansion;
  bool m_enable_echo;
  bool m_enable_echo_expanded;
  bool m_shell_is_interactive;
  bool m_error_exit;

  /* The single-letter option flags for $-, built from the flags above. */
  std::string option_flags_string() const;

  std::string expand_variable(const std::string &name) const;

  /* Turn a word into fields, applying tilde, variable expansion, and IFS field
     splitting, but not globbing. */
  std::vector<GlobField> expand_word(const Word &word) const;

  std::vector<GlobField> expand_path_once(const GlobField &field,
                                          bool should_expand_files);
  std::vector<GlobField>
  expand_path_recurse(const std::vector<GlobField> &fields);
  std::vector<std::string> expand_path(GlobField field);

  void expand_tilde(WordSegment &leading_segment) const;
};

/* Lower-level execution context. Path is the program path to execute, expanded
 * from program. Program is non-altered first arg. */
struct ExecContext
{
  static ExecContext make_from(SourceLocation location,
                               const std::vector<std::string> &args);

  std::optional<os::descriptor> in_fd{std::nullopt};
  std::optional<os::descriptor> out_fd{std::nullopt};

  bool is_builtin() const;

  const std::vector<std::string> &args() const;
  const std::string &program() const;
  const SourceLocation &source_location() const;

  void close_fds();
  void print_to_stdout(const std::string &s) const;

  i32 execute(bool is_async);

  const std::filesystem::path &program_path() const;
  const Builtin::Kind &builtin_kind() const;

private:
  /* clang-format off */
  ExecContext(SourceLocation location,
              std::variant<shit::Builtin::Kind, std::filesystem::path> &&kind,
              const std::vector<std::string> &args);
  /* clang-format on */

  std::variant<shit::Builtin::Kind, std::filesystem::path> m_kind;

  SourceLocation m_location;
  std::vector<std::string> m_args;
};

} /* namespace shit */
