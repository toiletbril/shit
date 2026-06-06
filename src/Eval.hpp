#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "Containers.hpp"
#include "Errors.hpp"
#include "Maybe.hpp"
#include "Path.hpp"
#include "ResolvedCommand.hpp"

#include <string>

namespace shit {

/* A field is a candidate argument after variable expansion and field splitting.
   It carries a parallel mask that marks which characters may act as glob
   metacharacters, so globbing needs no source-position arithmetic. The text
   lives on the scratch arena, since it lasts only as long as one command's
   expansion, while the mask stays a heap bit vector so glob_matches reads it
   unchanged. */
struct GlobField
{
  explicit GlobField(Allocator allocator)
      : text(allocator), glob_active(heap_allocator())
  {}

  String text;
  /* The mask grows by repeated push, so it stays on the heap allocator where a
     grow frees the old buffer, rather than the bump arena where every grow
     would leak. */
  ArrayList<bool> glob_active;
};

struct Token;
struct Word;
struct WordSegment;
struct Expression;

/* A pending non-local jump the evaluator carries instead of throwing. The break
   and continue builtins request a loop jump, return requests a function jump,
   and exit inside a subshell requests an exit. The tree-walk checks for a
   pending request after each child and either consumes it at the matching
   boundary or leaves it pending so an outer node consumes it. */
struct ControlFlow
{
  enum class Kind : u8
  {
    /* No jump is pending, so evaluation proceeds normally. */
    Normal,
    Break,
    Continue,
    Return,
    Exit,
  };

  Kind kind{Kind::Normal};
  /* The loop level for break and continue, or the status for return and exit.
   */
  i64 value{0};
  /* Where the requesting builtin sits, so an escaped jump points a caret at it.
     The source the offset indexes lives in source, set when the jump is made.
   */
  SourceLocation location{0, 0};
  const String *source{nullptr};
  String origin{};
};

/* A variable binding saved when a local shadows it. The previous value is
   None when the name was unset, so leaving the scope restores the unset
   state rather than an empty string. */
struct LocalBinding
{
  String name;
  Maybe<String> previous_value;
};

/* A background job, one entry in the job table. The id is the number jobs and
   fg name with a percent, such as %1. The pid is the process group leader so a
   signal reaches every stage of a pipeline. */
struct Job
{
  enum class State : u8
  {
    Running,
    Stopped,
    Done,
  };

  int id;
  os::process pid;
  String command;
  State state{State::Running};
  i32 last_status{0};
};

/* A snapshot of the mutable shell state, taken around a subshell or a command
   substitution so a cd or an assignment inside does not leak to the parent. */
struct EvalStateSnapshot
{
  HashMap<String> shell_variables;
  HashMap<const Expression *> functions;
  ArrayList<String> positional_params;
  Path working_directory;
};

struct EvalContext
{
  EvalContext(bool should_disable_path_expansion, bool should_echo,
              bool should_echo_expanded, bool shell_is_interactive,
              bool should_error_exit = false, String shell_name = {},
              ArrayList<String> positional_params = {});

  fn add_expansion() -> void;
  fn add_evaluated_expression() -> void;

  fn end_command() -> void;

  /* Variable expand, tilde expand, field split, and glob each token. */
  fn process_args(const ArrayList<const Token *> &args) -> ArrayList<String>;

  /* The allocator for transient expansion data, which a bump arena hands out
     and reclaims whole when the command ends. */
  fn scratch_allocator() -> Allocator
  {
    return bump_allocator(m_scratch_arena);
  }
  /* Reclaim the scratch arena, called between top-level commands. */
  fn reset_scratch_arena() -> void { m_scratch_arena.reset(); }

  fn set_shell_variable(StringView name, StringView value) -> void;
  fn unset_shell_variable(StringView name) -> void;
  fn get_variable_value(StringView name) const -> Maybe<String>;

  /* The stored value of a plain shell variable, or nullptr when the name is
     unset or names a special parameter. The pointer reads the value without a
     copy and stays valid until the next assignment to that name. */
  fn lookup_shell_variable(StringView name) const -> const String *
  {
    return m_shell_variables.find(name);
  }

  /* The positional parameters, $1 upward, with $0 separate as the shell name.
   */
  fn positional_params() const -> const ArrayList<String> &;
  fn set_positional_params(ArrayList<String> params) -> void;

  fn set_last_exit_status(i32 status) -> void;
  fn last_exit_status() const -> i32;

  /* The process id of the most recent background command, for $!. */
  fn set_last_background_pid(i64 pid) -> void;

  /* The job table tracks the background commands started with the & operator so
     jobs, fg, bg, wait, and kill can act on them. register_job adds a running
     job and returns its id. update_jobs polls every job without blocking and
     marks the ones that finished or stopped. */
  fn register_job(os::process pid, StringView command) -> int;
  fn update_jobs() -> void;
  fn jobs() -> ArrayList<Job> &;
  fn find_job(int id) -> Job *;
  fn most_recent_job() -> Job *;
  fn forget_done_jobs() -> void;

  /* monitor mode is set -m. It is on by default in an interactive shell, and it
     gates the terminal handoff so a non-interactive run never touches the
     controlling terminal. */
  fn set_monitor(bool enabled) -> void;
  fn monitor() const -> bool;

  /* Shell functions live in the parse arena, so the table is cleared before
     each top-level parse to avoid pointing at freed storage. A function shadows
     a builtin and a program of the same name. */
  fn register_function(StringView name, const Expression *body) -> void;
  fn find_function(StringView name) const -> const Expression *;
  fn has_functions() const -> bool;
  fn unset_function(StringView name) -> void;
  fn clear_functions() -> void;

  /* The names of every defined function, so the prepass of a later command
     knows a function defined earlier resolves. */
  fn function_names() const -> HashSet;

  /* trap stores an action to run for a condition, keyed by the condition name
     such as EXIT or INT. The EXIT action runs once when the shell ends. */
  fn set_trap(StringView condition, StringView action) -> void;
  fn remove_trap(StringView condition) -> void;
  fn traps() const -> const HashMap<String> &;
  fn run_exit_trap() -> void;

  /* readonly marks a variable so a later assignment to it fails. The set is
     usually empty, so set_shell_variable only scans it when it is not. */
  fn mark_readonly(StringView name) -> void;
  fn is_readonly(StringView name) const -> bool;
  fn readonly_names() const -> ArrayList<String>;

  /* A function call pushes a local scope so a local builtin inside it can
     shadow a variable and have the old value restored when the call returns.
     declare_local records the current binding of a name in the innermost scope
     before the caller overwrites it. */
  fn enter_function_scope() -> void;
  fn leave_function_scope() -> void;
  fn in_function_scope() const -> bool;
  fn declare_local(StringView name) -> void;

  /* alias maps a command word to its replacement text, consulted by the parser
     before a simple command. */
  fn set_alias(StringView name, StringView value) -> void;
  fn remove_alias(StringView name) -> bool;
  fn get_alias(StringView name) const -> Maybe<String>;
  fn alias_definitions() const -> ArrayList<String>;
  /* The defined alias names, so the prepass treats a use of one as resolvable.
   */
  fn alias_names() const -> HashSet;

  /* Save and restore the mutable state around a subshell or a command
     substitution, so changes inside do not leak to the parent. */
  fn snapshot_state() const -> EvalStateSnapshot;
  fn restore_state(EvalStateSnapshot snapshot) -> void;

  /* Track whether evaluation is inside a subshell or a command substitution, so
     the exit builtin ends only that scope instead of the whole shell. */
  fn enter_subshell() -> void;
  fn leave_subshell() -> void;
  fn in_subshell() const -> bool;

  /* The control-flow channel the break, continue, return, and exit builtins
     write instead of throwing. A request records where it was made against the
     current source, so an escaped jump points a caret at the builtin. The
     tree-walk checks has_pending_control_flow after each child and consumes the
     request at the matching loop, function, or subshell boundary. */
  fn request_break(i64 level, SourceLocation location) -> void;
  fn request_continue(i64 level, SourceLocation location) -> void;
  fn request_return(i64 status, SourceLocation location) -> void;
  fn request_exit(i64 status, SourceLocation location) -> void;
  fn has_pending_control_flow() const -> bool;
  fn pending_control_flow() -> ControlFlow &;
  fn pending_control_flow() const -> const ControlFlow &;
  fn clear_control_flow() -> void;

  /* The source currently being evaluated and its human name, so a control-flow
     request or a deferred report formats a caret against the right text. Set
     around a top-level run and around a sourced run, restoring the previous
     frame afterwards. */
  fn set_current_source(const String *source, String origin) -> void;
  fn current_source() const -> const String *;
  fn current_origin() const -> const String &;

  /* The set builtin toggles these options at run time. error_exit is set -e,
     echo_expanded is set -x, and error_unset is set -u. */
  fn set_error_exit(bool enabled) -> void;
  fn error_exit() const -> bool;
  fn set_echo_expanded(bool enabled) -> void;
  fn set_error_unset(bool enabled) -> void;
  fn error_unset() const -> bool;

  /* noclobber rejects an overwrite of an existing file through a plain >, set
     by -C and set -o noclobber. */
  fn set_no_clobber(bool enabled) -> void;
  fn no_clobber() const -> bool;
  /* allexport marks every later assignment for the environment, set by -a and
     set -o allexport. */
  fn set_export_all(bool enabled) -> void;
  fn export_all() const -> bool;
  /* noglob disables pathname expansion, set by -f and set -o noglob, and shares
     the path-expansion flag the field splitting already reads. */
  fn set_no_glob(bool enabled) -> void;
  fn no_glob() const -> bool;
  /* noexec parses without running, set by -n and set -o noexec. */
  fn set_no_exec(bool enabled) -> void;
  fn no_exec() const -> bool;

  /* A condition, such as an if test or an && operand, suppresses set -e while
     it runs, since its failure is expected. */
  fn enter_condition() -> void;
  fn leave_condition() -> void;
  fn in_condition() const -> bool;

  /* The name=value lines that set with no argument prints, sorted. */
  fn sorted_variable_assignments() const -> ArrayList<String>;

  /* Expand a word in assignment context, with variable, tilde, and command
     substitution but no field splitting and no globbing. */
  fn expand_word_for_assignment(const Word &word) -> String;

  /* Run the source of a $(...) and return its standard output with trailing
     newlines stripped. The inner command runs in-process with the working
     directory and variables snapshotted, so a cd or an assignment inside does
     not leak to the parent. */
  fn capture_command_substitution(const String &source) -> String;

  /* Lex, parse, and evaluate a source string in the current context, without
     capturing output or snapshotting state. The eval and dot builtins use this,
     so a break, a return, or an assignment inside acts on the caller. */
  /* Lex, parse, and evaluate a chunk of source in this context. A dot-source
     consumes a return at the top of the chunk and ends there, while an eval
     leaves the return pending so it propagates to the enclosing function or the
     shell, which is why consume_return is false for eval. */
  fn run_source(StringView source, StringView origin = "a sourced command",
                bool consume_return = true) -> i32;

  /* getopts keeps the position inside the current argument here, so a grouped
     option such as -abc is parsed one letter per call. last_optind detects when
     the script reset OPTIND to start a fresh scan. */
  fn getopts_char_index() const -> usize;
  fn set_getopts_char_index(usize index) -> void;
  fn getopts_last_optind() const -> i64;
  fn set_getopts_last_optind(i64 optind) -> void;

  /* Destroy the ASTs retained from eval and dot. The caller does this before it
     resets the parse arena, so a function those sources defined stays valid for
     the rest of the current top-level command. */
  fn clear_retained_sources() -> void;

  /* Keep a top-level command's tree alive past its run, so a function it
     defined stays callable on the next command. The retained trees are
     destroyed by clear_retained_sources while the arena is still valid. */
  fn retain_ast(Expression *ast) -> void;

  /* Expand a heredoc body, its $name and ${...} references, keeping the literal
     text and newlines. */
  fn expand_heredoc_body(StringView body) -> String;

  fn expand_modifier_word(StringView word, bool remove_quotes = true) -> String;

  fn should_echo() const -> bool;
  fn should_echo_expanded() const -> bool;
  fn shell_is_interactive() const -> bool;

  fn make_stats_string() const -> String;

  fn last_expressions_executed() const -> usize;
  fn total_expressions_executed() const -> usize;

  fn last_expansion_count() const -> usize;
  fn total_expansion_count() const -> usize;

protected:
  usize m_expressions_executed_last{0};
  usize m_expressions_executed_total{0};
  usize m_expansions_last{0};
  usize m_expansions_total{0};

  BumpArena m_scratch_arena{};
  HashMap<String> m_shell_variables{heap_allocator()};
  /* The cached value of IFS, kept current by set_shell_variable, so word
     splitting does not look it up in the map or the environment per word. */
  String m_field_separators{" \t\n"};

  /* A byte-indexed table that answers whether a character is a field separator
     in one load, instead of scanning IFS per byte. The layout is a contiguous
     256-byte block, the shape a later SIMD scan reads to find separators in
     bulk. It is rebuilt whenever IFS changes. */
  bool m_field_separator_table[256]{};
  /* Set IFS and refresh the separator table together, so the table never drifts
     from the cached value. */
  fn set_field_separators(StringView value) -> void;
  fn is_field_separator(char c) const -> bool;
  i32 m_last_exit_status{0};

  String m_shell_name{};
  ArrayList<String> m_positional_params{heap_allocator()};
  Maybe<i64> m_last_background_pid{};
  HashMap<const Expression *> m_functions{heap_allocator()};
  usize m_subshell_depth{0};
  usize m_condition_depth{0};

  /* The pending non-local jump, Normal when none is pending. */
  ControlFlow m_control_flow{};
  /* The source and name of the text being evaluated, for caret formatting. */
  const String *m_current_source{nullptr};
  String m_current_origin{};

  /* ASTs from eval and dot, kept alive until the next top-level command so a
     function they define survives the rest of the current one. */
  ArrayList<Expression *> m_retained_source_asts{heap_allocator()};

  /* The source text of each eval and dot run, kept alive for as long as the
     ASTs above. A control-flow jump made inside one points its caret at this
     text, so the pointer must outlive the run that escaped it. */
  /* The retained source buffers are heap-owned pointers, not inline elements,
     so a nested run_source that grows the list never moves an earlier buffer
     and leaves m_current_source or a ControlFlow::source dangling. */
  ArrayList<String *> m_retained_sources{heap_allocator()};

  bool m_error_unset{false};
  bool m_no_clobber{false};
  bool m_export_all{false};
  bool m_no_exec{false};
  usize m_getopts_char_index{1};
  i64 m_getopts_last_optind{0};
  HashMap<String> m_traps{heap_allocator()};
  bool m_exit_trap_ran{false};

  /* The names marked read-only, scanned by set_shell_variable only when the
     list is not empty. */
  ArrayList<String> m_readonly_names{heap_allocator()};
  /* The alias name to replacement table. */
  HashMap<String> m_aliases{heap_allocator()};
  /* One entry per active function call, holding the bindings a local shadowed
     so leaving the call restores them. */
  ArrayList<ArrayList<LocalBinding>> m_local_scopes{heap_allocator()};

  /* The background jobs and the id to give the next one. */
  ArrayList<Job> m_jobs{heap_allocator()};
  int m_next_job_id{1};
  bool m_monitor{false};
  bool m_enable_path_expansion;
  bool m_enable_echo;
  bool m_enable_echo_expanded;
  bool m_shell_is_interactive;
  bool m_error_exit;

  /* The single-letter option flags for $-, built from the flags above. */
  fn option_flags_string() const -> String;

  fn expand_variable(StringView name) const -> String;

  /* Write a variable without the read-only check, for restoring a shadowed
     local on function return where a throw from a noexcept defer would
     terminate the shell. */
  fn assign_variable(StringView name, StringView value) -> void;

  /* Expand a ${...} body, which is a plain name or a name with a length, a
     default, an alternate, an assign, an error, or a prefix or suffix trim. */
  fn apply_parameter_expansion(StringView spec) -> String;

  /* Compute the integer value of a $((...)) expression, resolving shell
     variables and applying any assignments inside it. */
  fn evaluate_arithmetic(StringView expression) -> i64;

  /* Turn a word into fields, applying tilde, variable expansion, command
     substitution, and IFS field splitting, but not globbing. */
  fn expand_word(const Word &word) -> ArrayList<GlobField>;

  fn expand_path_once(const GlobField &field, bool should_expand_files)
      -> ArrayList<GlobField>;
  fn expand_path_recurse(ArrayList<GlobField> fields) -> ArrayList<GlobField>;
  fn expand_path(GlobField field) -> ArrayList<String>;

  fn expand_tilde(WordSegment &leading_segment) const -> void;
};

/* Lower-level execution context. Path is the program path to execute, expanded
 * from program. Program is non-altered first arg. */
struct ExecContext
{
  static fn make_from(SourceLocation location, const ArrayList<String> &args)
      -> ExecContext;

  /* Build directly from an already resolved builtin kind or program path,
     skipping the PATH search. A simple command memoizes its resolution and
     reuses it across the iterations of a loop. */
  static fn from_resolved(SourceLocation location, ResolvedCommand kind,
                          const ArrayList<String> &args) -> ExecContext;

  Maybe<os::descriptor> in_fd{};
  Maybe<os::descriptor> out_fd{};
  Maybe<os::descriptor> err_fd{};

  /* 2>&1 routes the standard error to wherever the standard output goes, and
     1>&2 the reverse. Applied after the file descriptors are placed. */
  bool dup_err_to_out{false};
  bool dup_out_to_err{false};

  fn is_builtin() const -> bool;

  fn args() const -> const ArrayList<String> &;
  fn program() const -> const String &;
  fn source_location() const -> const SourceLocation &;

  fn close_fds() -> void;
  fn print_to_stdout(StringView s) const -> void;

  fn execute(bool is_async) -> i32;

  fn program_path() const -> const Path &;
  fn builtin_kind() const -> const Builtin::Kind &;

private:
  ExecContext(SourceLocation location, ResolvedCommand &&kind,
              const ArrayList<String> &args);

  ResolvedCommand m_kind;

  SourceLocation m_location;
  ArrayList<String> m_args{heap_allocator()};
};

} /* namespace shit */
