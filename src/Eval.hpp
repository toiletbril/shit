#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "Maybe.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

/* The return builtin throws this to unwind to the enclosing function call. */
struct FunctionReturn
{
  i64 status;
};

/* The exit builtin throws this inside a subshell or a command substitution, so
   it ends only that and not the whole shell. At the top level exit really
   exits. */
struct ShellExit
{
  i64 status;
};

/* A snapshot of the mutable shell state, taken around a subshell or a command
   substitution so a cd or an assignment inside does not leak to the parent. */
struct EvalStateSnapshot
{
  std::unordered_map<std::string, std::string> shell_variables;
  std::unordered_map<std::string, const Expression *> functions;
  std::vector<std::string> positional_params;
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
  Maybe<std::string> get_variable_value(const std::string &name) const;

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
  bool has_functions() const;
  void unset_function(const std::string &name);
  void clear_functions();

  /* The names of every defined function, so the prepass of a later command
     knows a function defined earlier resolves. */
  std::unordered_set<std::string> function_names() const;

  /* trap stores an action to run for a condition, keyed by the condition name
     such as EXIT or INT. The EXIT action runs once when the shell ends. */
  void set_trap(const std::string &condition, const std::string &action);
  void remove_trap(const std::string &condition);
  const std::unordered_map<std::string, std::string> &traps() const;
  void run_exit_trap();

  /* Save and restore the mutable state around a subshell or a command
     substitution, so changes inside do not leak to the parent. */
  EvalStateSnapshot snapshot_state() const;
  void restore_state(EvalStateSnapshot snapshot);

  /* Track whether evaluation is inside a subshell or a command substitution, so
     the exit builtin ends only that scope instead of the whole shell. */
  void enter_subshell();
  void leave_subshell();
  bool in_subshell() const;

  /* The set builtin toggles these options at run time. error_exit is set -e,
     echo_expanded is set -x, and error_unset is set -u. */
  void set_error_exit(bool enabled);
  bool error_exit() const;
  void set_echo_expanded(bool enabled);
  void set_error_unset(bool enabled);
  bool error_unset() const;

  /* A condition, such as an if test or an && operand, suppresses set -e while it
     runs, since its failure is expected. */
  void enter_condition();
  void leave_condition();
  bool in_condition() const;

  /* The name=value lines that set with no argument prints, sorted. */
  std::vector<std::string> sorted_variable_assignments() const;

  /* Expand a word in assignment context, with variable, tilde, and command
     substitution but no field splitting and no globbing. */
  std::string expand_word_for_assignment(const Word &word);

  /* Run the source of a $(...) and return its standard output with trailing
     newlines stripped. The inner command runs in-process with the working
     directory and variables snapshotted, so a cd or an assignment inside does
     not leak to the parent. */
  std::string capture_command_substitution(const std::string &source);

  /* Lex, parse, and evaluate a source string in the current context, without
     capturing output or snapshotting state. The eval and dot builtins use this,
     so a break, a return, or an assignment inside acts on the caller. */
  i32 run_source(const std::string &source,
                 const std::string &origin = "a sourced command");

  /* getopts keeps the position inside the current argument here, so a grouped
     option such as -abc is parsed one letter per call. last_optind detects when
     the script reset OPTIND to start a fresh scan. */
  usize getopts_char_index() const;
  void set_getopts_char_index(usize index);
  i64 getopts_last_optind() const;
  void set_getopts_last_optind(i64 optind);

  /* Destroy the ASTs retained from eval and dot. The caller does this before it
     resets the parse arena, so a function those sources defined stays valid for
     the rest of the current top-level command. */
  void clear_retained_sources();

  /* Keep a top-level command's tree alive past its run, so a function it defined
     stays callable on the next command. The retained trees are destroyed by
     clear_retained_sources while the arena is still valid. */
  void retain_ast(Expression *ast);

  /* Expand a heredoc body, its $name and ${...} references, keeping the literal
     text and newlines. */
  std::string expand_heredoc_body(const std::string &body);

  std::string expand_modifier_word(const std::string &word,
                                   bool remove_quotes = true);

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
  /* The cached value of IFS, kept current by set_shell_variable, so word
     splitting does not look it up in the map or the environment per word. */
  std::string m_field_separators{" \t\n"};
  i32 m_last_exit_status{0};

  std::string m_shell_name{};
  std::vector<std::string> m_positional_params{};
  Maybe<i64> m_last_background_pid{};
  std::unordered_map<std::string, const Expression *> m_functions{};
  usize m_subshell_depth{0};
  usize m_condition_depth{0};

  /* ASTs from eval and dot, kept alive until the next top-level command so a
     function they define survives the rest of the current one. */
  std::vector<Expression *> m_retained_source_asts{};

  bool m_error_unset{false};
  usize m_getopts_char_index{1};
  i64 m_getopts_last_optind{0};
  std::unordered_map<std::string, std::string> m_traps{};
  bool m_exit_trap_ran{false};
  bool m_enable_path_expansion;
  bool m_enable_echo;
  bool m_enable_echo_expanded;
  bool m_shell_is_interactive;
  bool m_error_exit;

  /* The single-letter option flags for $-, built from the flags above. */
  std::string option_flags_string() const;

  std::string expand_variable(const std::string &name) const;

  /* Expand a ${...} body, which is a plain name or a name with a length, a
     default, an alternate, an assign, an error, or a prefix or suffix trim. */
  std::string apply_parameter_expansion(const std::string &spec);

  /* Compute the integer value of a $((...)) expression, resolving shell
     variables and applying any assignments inside it. */
  i64 evaluate_arithmetic(const std::string &expression);

  /* Turn a word into fields, applying tilde, variable expansion, command
     substitution, and IFS field splitting, but not globbing. */
  std::vector<GlobField> expand_word(const Word &word);

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

  /* Build directly from an already resolved builtin kind or program path,
     skipping the PATH search. A simple command memoizes its resolution and
     reuses it across the iterations of a loop. */
  static ExecContext
  from_resolved(SourceLocation location,
                std::variant<shit::Builtin::Kind, std::filesystem::path> kind,
                const std::vector<std::string> &args);

  Maybe<os::descriptor> in_fd{};
  Maybe<os::descriptor> out_fd{};
  Maybe<os::descriptor> err_fd{};

  /* 2>&1 routes the standard error to wherever the standard output goes, and
     1>&2 the reverse. Applied after the file descriptors are placed. */
  bool dup_err_to_out{false};
  bool dup_out_to_err{false};

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
