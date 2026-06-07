#pragma once

#include "Arena.hpp"
#include "Builtin.hpp"
#include "Common.hpp"
#include "Containers.hpp"
#include "Errors.hpp"
#include "Maybe.hpp"
#include "Path.hpp"
#include "ResolvedCommand.hpp"

namespace shit {

/* A field is a candidate argument after variable expansion and field splitting.
   It carries a parallel mask that marks which characters may act as glob
   metacharacters, so globbing needs no source-position arithmetic. The text
   lives on the scratch arena, since it lasts only as long as one command's
   expansion, while the mask stays a heap bit vector so glob_matches reads it
   unchanged. */
struct glob_field
{
  explicit glob_field(Allocator allocator)
      : text(allocator), glob_active(heap_allocator())
  {}

  String text;
  /* The mask grows by repeated push, so it stays on the heap allocator where a
     grow frees the old buffer, rather than the bump arena where every grow
     would leak. */
  ArrayList<bool> glob_active;
};

class Token;
class Word;
class WordSegment;
class Expression;

/* A pending non-local jump the evaluator carries instead of throwing. The break
   and continue builtins request a loop jump, return requests a function jump,
   and exit inside a subshell requests an exit. The tree-walk checks for a
   pending request after each child and either consumes it at the matching
   boundary or leaves it pending so an outer node consumes it. */
struct control_flow
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

/* One frame of the source backtrace, pushed when run_source begins and popped
   when it returns. The origin names the call descriptively, the call_site is
   where the dot or eval sits in its parent, and parent_source is the text that
   call site lives in so a caret renders against it. The parent_source is None
   for a top-level source whose call site has no surrounding text. */
struct source_frame
{
  source_frame(String origin, SourceLocation call_site,
               const String *parent_source)
      : origin(steal(origin)), call_site(call_site),
        parent_source(parent_source)
  {}

  String origin;
  SourceLocation call_site;
  const String *parent_source;
};

/* A variable binding saved when a local shadows it. The previous value is
   None when the name was unset, so leaving the scope restores the unset
   state rather than an empty string. */
struct local_binding
{
  String name;
  Maybe<String> previous_value;
};

/* A background job, one entry in the job table. The id is the number jobs and
   fg name with a percent, such as %1. The pid is the process group leader so a
   signal reaches every stage of a pipeline. */
struct job
{
  enum class State : u8
  {
    Running,
    Stopped,
    Done,
  };

  i32 id;
  os::process pid;
  String command;
  State state{State::Running};
  i32 last_status{0};
};

/* A snapshot of the mutable shell state, taken around a subshell or a command
   substitution so a cd or an assignment inside does not leak to the parent. */
struct eval_state_snapshot
{
  HashMap<String> shell_variables;
  HashMap<const Expression *> functions;
  ArrayList<String> positional_params;
  Path working_directory;
};

class EvalContext
{
public:
  EvalContext(bool should_disable_path_expansion, bool should_echo,
              bool should_echo_expanded, bool shell_is_interactive,
              bool should_error_exit = false, String shell_name = {},
              ArrayList<String> positional_params = {});

  fn add_expansion() wontthrow -> void;
  fn add_evaluated_expression() wontthrow -> void;

  fn end_command() wontthrow -> void;

  /* Variable expand, tilde expand, field split, and glob each token. */
  fn process_args(const ArrayList<const Token *> &args) throws
      -> ArrayList<String>;

  /* The allocator for transient expansion data, which a bump arena hands out
     and reclaims whole when the command ends. */
  fn scratch_allocator() wontthrow -> Allocator
  {
    return bump_allocator(m_scratch_arena);
  }
  /* Reclaim the scratch arena, called between top-level commands. */
  fn reset_scratch_arena() wontthrow -> void { m_scratch_arena.reset(); }

  fn set_shell_variable(StringView name, StringView value) throws -> void;
  fn unset_shell_variable(StringView name) throws -> void;
  fn get_variable_value(StringView name) const throws -> Maybe<String>;

  /* The stored value of a plain shell variable, or nullptr when the name is
     unset or names a special parameter. The pointer reads the value without a
     copy and stays valid until the next assignment to that name. */
  hot fn lookup_shell_variable(StringView name) const wontthrow
      -> const String *
  {
    return m_shell_variables.find(name);
  }

  /* The positional parameters, $1 upward, with $0 separate as the shell name.
   */
  pure fn positional_params() const wontthrow -> const ArrayList<String> &;
  fn set_positional_params(ArrayList<String> params) wontthrow -> void;

  fn set_last_exit_status(i32 status) wontthrow -> void;
  pure fn last_exit_status() const wontthrow -> i32;

  /* The process id of the most recent background command, for $!. */
  fn set_last_background_pid(i64 pid) wontthrow -> void;

  /* The job table tracks the background commands started with the & operator so
     jobs, fg, bg, wait, and kill can act on them. register_job adds a running
     job and returns its id. update_jobs polls every job without blocking and
     marks the ones that finished or stopped. */
  fn register_job(os::process pid, StringView command) throws -> i32;
  fn update_jobs() throws -> void;
  fn jobs() wontthrow -> ArrayList<job> &;
  fn find_job(i32 id) wontthrow -> job *;
  fn most_recent_job() wontthrow -> job *;
  fn forget_done_jobs() throws -> void;

  /* monitor mode is set -m. It is on by default in an interactive shell, and it
     gates the terminal handoff so a non-interactive run never touches the
     controlling terminal. */
  fn set_monitor(bool enabled) wontthrow -> void;
  pure fn monitor() const wontthrow -> bool;

  /* Shell functions live in the parse arena, so the table is cleared before
     each top-level parse to avoid pointing at freed storage. A function shadows
     a builtin and a program of the same name. */
  fn register_function(StringView name, const Expression *body) throws -> void;
  fn find_function(StringView name) const wontthrow -> const Expression *;
  pure fn has_functions() const wontthrow -> bool;
  fn unset_function(StringView name) throws -> void;
  fn clear_functions() wontthrow -> void;

  /* The names of every defined function, so the prepass of a later command
     knows a function defined earlier resolves. */
  fn function_names() const throws -> HashSet;

  /* trap stores an action to run for a condition, keyed by the condition name
     such as EXIT or INT. The EXIT action runs once when the shell ends. */
  fn set_trap(StringView condition, StringView action) throws -> void;
  fn remove_trap(StringView condition) throws -> void;
  pure fn traps() const wontthrow -> const HashMap<String> &;
  fn run_exit_trap() throws -> void;
  /* True when an EXIT trap action is set, so the run loop keeps the fork for a
     terminal command and lets the trap run before the shell exits. */
  fn has_exit_trap() const wontthrow -> bool;

  /* readonly marks a variable so a later assignment to it fails. The set is
     usually empty, so set_shell_variable only scans it when it is not. */
  fn mark_readonly(StringView name) throws -> void;
  fn is_readonly(StringView name) const wontthrow -> bool;
  fn readonly_names() const throws -> ArrayList<String>;

  /* A function call pushes a local scope so a local builtin inside it can
     shadow a variable and have the old value restored when the call returns.
     declare_local records the current binding of a name in the innermost scope
     before the caller overwrites it. */
  fn enter_function_scope() throws -> void;
  fn leave_function_scope() throws -> void;
  pure fn in_function_scope() const wontthrow -> bool;
  fn declare_local(StringView name) throws -> void;

  /* alias maps a command word to its replacement text, consulted by the parser
     before a simple command. */
  fn set_alias(StringView name, StringView value) throws -> void;
  fn remove_alias(StringView name) throws -> bool;
  fn get_alias(StringView name) const throws -> Maybe<String>;
  fn alias_definitions() const throws -> ArrayList<String>;
  /* The defined alias names, so the prepass treats a use of one as resolvable.
   */
  fn alias_names() const throws -> HashSet;

  /* Save and restore the mutable state around a subshell or a command
     substitution, so changes inside do not leak to the parent. */
  fn snapshot_state() const throws -> eval_state_snapshot;
  fn restore_state(eval_state_snapshot snapshot) throws -> void;

  /* Track whether evaluation is inside a subshell or a command substitution, so
     the exit builtin ends only that scope instead of the whole shell. */
  fn enter_subshell() wontthrow -> void;
  fn leave_subshell() wontthrow -> void;
  pure fn in_subshell() const wontthrow -> bool;

  /* The control-flow channel the break, continue, return, and exit builtins
     write instead of throwing. A request records where it was made against the
     current source, so an escaped jump points a caret at the builtin. The
     tree-walk checks has_pending_control_flow after each child and consumes the
     request at the matching loop, function, or subshell boundary. */
  fn request_break(i64 level, SourceLocation location) throws -> void;
  fn request_continue(i64 level, SourceLocation location) throws -> void;
  fn request_return(i64 status, SourceLocation location) throws -> void;
  fn request_exit(i64 status, SourceLocation location) throws -> void;
  pure fn has_pending_control_flow() const wontthrow -> bool;
  fn pending_control_flow() wontthrow -> control_flow &;
  pure fn pending_control_flow() const wontthrow -> const control_flow &;
  fn clear_control_flow() wontthrow -> void;

  /* The source currently being evaluated and its human name, so a control-flow
     request or a deferred report formats a caret against the right text. Set
     around a top-level run and around a sourced run, restoring the previous
     frame afterwards. */
  fn set_current_source(const String *source, String origin) wontthrow -> void;
  pure fn current_source() const wontthrow -> const String *;
  pure fn current_origin() const wontthrow -> const String &;

  /* The byte offset in the current source of the command being evaluated, the
     position $LINENO reports the line of. A SimpleCommand and an assignment set
     it as evaluation reaches them, which is the statement granularity autoconf
     reads $LINENO at. */
  fn set_current_location(SourceLocation location) wontthrow -> void;

  /* The set builtin toggles these options at run time. error_exit is set -e,
     echo_expanded is set -x, and error_unset is set -u. */
  fn set_error_exit(bool enabled) wontthrow -> void;
  pure fn error_exit() const wontthrow -> bool;
  fn set_echo_expanded(bool enabled) wontthrow -> void;
  fn set_error_unset(bool enabled) wontthrow -> void;
  pure fn error_unset() const wontthrow -> bool;

  /* noclobber rejects an overwrite of an existing file through a plain >, set
     by -C and set -o noclobber. */
  fn set_no_clobber(bool enabled) wontthrow -> void;
  pure fn no_clobber() const wontthrow -> bool;
  /* allexport marks every later assignment for the environment, set by -a and
     set -o allexport. */
  fn set_export_all(bool enabled) wontthrow -> void;
  pure fn export_all() const wontthrow -> bool;
  /* noglob disables pathname expansion, set by -f and set -o noglob, and shares
     the path-expansion flag the field splitting already reads. */
  fn set_no_glob(bool enabled) wontthrow -> void;
  pure fn no_glob() const wontthrow -> bool;
  /* noexec parses without running, set by -n and set -o noexec. */
  fn set_no_exec(bool enabled) wontthrow -> void;
  pure fn no_exec() const wontthrow -> bool;
  /* failglob makes a glob that matches no path a hard error, the shell default
     that catches a typo. With it off the unmatched glob expands to its literal
     pattern the way POSIX and dash do, set by --no-fail-glob and set -o
     failglob. */
  fn set_failglob(bool enabled) wontthrow -> void;
  pure fn failglob() const wontthrow -> bool;

  /* A condition, such as an if test or an && operand, suppresses set -e while
     it runs, since its failure is expected. */
  fn enter_condition() wontthrow -> void;
  fn leave_condition() wontthrow -> void;
  pure fn in_condition() const wontthrow -> bool;

  /* The run loop sets this before the final chunk's evaluation when the shell
     will exit with that chunk's status and no EXIT trap is pending. A terminal
     external command then replaces the shell process instead of fork, exec, and
     wait, the way dash execs the last command under EV_EXIT. The flag rides only
     the tail position. A compound list clears it on every node but its last, and
     every other node clears it, so only a command whose status becomes the
     shell's status sees it set. */
  fn set_terminal_exec_allowed(bool enabled) wontthrow -> void;
  pure fn terminal_exec_allowed() const wontthrow -> bool;

  /* The name=value lines that set with no argument prints, sorted. */
  fn sorted_variable_assignments() const throws -> ArrayList<String>;

  /* Expand a word in assignment context, with variable, tilde, and command
     substitution but no field splitting and no globbing. */
  fn expand_word_for_assignment(const Word &word) throws -> String;

  /* Run the source of a $(...) and return its standard output with trailing
     newlines stripped. The inner command runs in-process with the working
     directory and variables snapshotted, so a cd or an assignment inside does
     not leak to the parent. */
  fn capture_command_substitution(const String &source) throws -> String;

  /* Same capture, but the segment caches its parsed inner command so a $(...)
     in a loop body is lexed and parsed once and re-evaluated thereafter. The
     evaluation still runs every call, so output and side effects are
     unchanged. */
  fn capture_command_substitution(const WordSegment &segment) throws -> String;

  /* Run a parsed inner command under the substitution machinery, capturing its
     stdout and snapshotting state so a cd or an assignment inside does not leak.
     Both capture overloads share this once they hold an AST. */
  fn run_captured_substitution(const Expression *ast) throws -> String;

  /* Lex, parse, and evaluate a source string in the current context, without
     capturing output or snapshotting state. The eval and dot builtins use this,
     so a break, a return, or an assignment inside acts on the caller. */
  /* Lex, parse, and evaluate a chunk of source in this context. A dot-source
     consumes a return at the top of the chunk and ends there, while an eval
     leaves the return pending so it propagates to the enclosing function or the
     shell, which is why consume_return is false for eval. */
  /* call_site names where the dot or eval that triggered this run sits in its
     own parent source, so a backtrace frame points a caret at it. It is None
     for a run with no surrounding source, such as the EXIT trap. filename is
     the bare path of the source being run, stamped into every location its
     lexer makes. */
  fn run_source(StringView source, StringView origin = "a sourced command",
                bool consume_return = true,
                Maybe<SourceLocation> call_site = None,
                Maybe<StringView> filename = None) throws -> i32;

  /* A guard around a nested dot-source or eval run, and one around a function
     call. Each increments a depth counter and throws a located error past the
     cap rather than recursing until memory is exhausted. The caller pairs the
     enter with a defer that calls the matching leave on every unwind path. */
  fn enter_source(SourceLocation location) throws -> void;
  fn leave_source() wontthrow -> void;
  fn enter_function_call(SourceLocation location) throws -> void;
  fn leave_function_call() wontthrow -> void;

  /* getopts keeps the position inside the current argument here, so a grouped
     option such as -abc is parsed one letter per call. last_optind detects when
     the script reset OPTIND to start a fresh scan. */
  pure fn getopts_char_index() const wontthrow -> usize;
  fn set_getopts_char_index(usize index) wontthrow -> void;
  pure fn getopts_last_optind() const wontthrow -> i64;
  fn set_getopts_last_optind(i64 optind) wontthrow -> void;

  /* Destroy the ASTs retained from eval and dot. The caller does this before it
     resets the parse arena, so a function those sources defined stays valid for
     the rest of the current top-level command. */
  fn clear_retained_sources() wontthrow -> void;

  /* Keep a top-level command's tree alive past its run, so a function it
     defined stays callable on the next command. The retained trees are
     destroyed by clear_retained_sources while the arena is still valid. */
  fn retain_ast(Expression *ast) throws -> void;

  /* Expand a heredoc body, its $name and ${...} references, keeping the literal
     text and newlines. */
  fn expand_heredoc_body(StringView body) throws -> String;

  fn expand_modifier_word(StringView word, bool remove_quotes = true) throws
      -> String;

  pure fn should_echo() const wontthrow -> bool;
  pure fn should_echo_expanded() const wontthrow -> bool;
  pure fn shell_is_interactive() const wontthrow -> bool;

  fn make_stats_string() const throws -> String;

  /* Stats counting is off unless -S asked for the report. The evaluate path
     tests this so the per-node bookkeeping never runs when nobody reads it. */
  fn set_stats_enabled(bool enabled) wontthrow -> void;
  pure fn stats_enabled() const wontthrow -> bool;

  pure fn last_expressions_executed() const wontthrow -> usize;
  pure fn total_expressions_executed() const wontthrow -> usize;

  pure fn last_expansion_count() const wontthrow -> usize;
  pure fn total_expansion_count() const wontthrow -> usize;

  pure fn commands_evaluated() const wontthrow -> usize;
  pure fn peak_ast_arena_bytes() const wontthrow -> usize;

protected:
  bool m_stats_enabled{false};
  usize m_expressions_executed_last{0};
  usize m_expressions_executed_total{0};
  usize m_expansions_last{0};
  usize m_expansions_total{0};
  usize m_commands_evaluated{0};
  /* The largest live AST arena footprint seen at the end of any command. The
     stats path samples the arena once per command, off the hot path. */
  usize m_peak_ast_arena_bytes{0};

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
  fn set_field_separators(StringView value) throws -> void;
  pure fn is_field_separator(char c) const wontthrow -> bool;
  i32 m_last_exit_status{0};

  String m_shell_name{};
  ArrayList<String> m_positional_params{heap_allocator()};
  Maybe<i64> m_last_background_pid{};
  HashMap<const Expression *> m_functions{heap_allocator()};
  usize m_subshell_depth{0};
  usize m_condition_depth{0};

  /* The nesting depth of dot-source and eval runs, and of function calls, each
     bounded so a runaway recursion errors with a located message rather than
     growing the native stack and the arena until the process is killed. A
     pathological autoconf self-test that regenerates and re-sources a file
     forever is the motivating case. */
  usize m_source_depth{0};
  usize m_function_call_depth{0};

  /* The pending non-local jump, Normal when none is pending. */
  control_flow m_control_flow{};
  /* The source and name of the text being evaluated, for caret formatting. */
  const String *m_current_source{nullptr};
  String m_current_origin{};

  /* The byte offset in m_current_source of the command being evaluated, read by
     $LINENO to report its line. It starts at zero so an interactive single
     line, whose source holds no newlines, reports line 1. */
  usize m_current_location_position{0};

  /* The chain of sourced-file and eval frames from the outermost down to the
     one running now, so an error deep in a nested source prints every call site
     rather than only the innermost. Each frame carries the call site and its
     parent text, so a backtrace renders a caret at the dot or eval in the
     parent. run_source pushes on entry, pops on exit. */
  ArrayList<source_frame> m_source_frames{heap_allocator()};

  /* ASTs from eval and dot, kept alive until the next top-level command so a
     function they define survives the rest of the current one. */
  ArrayList<Expression *> m_retained_source_asts{heap_allocator()};

  /* The source text of each eval and dot run, kept alive for as long as the
     ASTs above. A control-flow jump made inside one points its caret at this
     text, so the pointer must outlive the run that escaped it. */
  /* The retained source buffers are heap-owned pointers, not inline elements,
     so a nested run_source that grows the list never moves an earlier buffer
     and leaves m_current_source or a control_flow::source dangling. */
  ArrayList<String *> m_retained_sources{heap_allocator()};

  bool m_error_unset{false};
  bool m_no_clobber{false};
  bool m_export_all{false};
  bool m_no_exec{false};
  bool m_failglob{true};
  usize m_getopts_char_index{1};
  i64 m_getopts_last_optind{0};
  HashMap<String> m_traps{heap_allocator()};
  bool m_exit_trap_ran{false};
  bool m_terminal_exec_allowed{false};

  /* The names marked read-only, scanned by set_shell_variable only when the
     list is not empty. */
  ArrayList<String> m_readonly_names{heap_allocator()};
  /* The alias name to replacement table. */
  HashMap<String> m_aliases{heap_allocator()};
  /* One entry per active function call, holding the bindings a local shadowed
     so leaving the call restores them. */
  ArrayList<ArrayList<local_binding>> m_local_scopes{heap_allocator()};

  /* The background jobs and the id to give the next one. */
  ArrayList<job> m_jobs{heap_allocator()};
  i32 m_next_job_id{1};
  bool m_monitor{false};
  bool m_enable_path_expansion;
  bool m_enable_echo;
  bool m_enable_echo_expanded;
  bool m_shell_is_interactive;
  bool m_error_exit;

  /* The single-letter option flags for $-, built from the flags above. */
  fn option_flags_string() const throws -> String;

  fn expand_variable(StringView name) const throws -> String;

  /* Write a variable without the read-only check, for restoring a shadowed
     local on function return where a throw from a noexcept defer would
     terminate the shell. */
  fn assign_variable(StringView name, StringView value) throws -> void;

  /* Expand a ${...} body, which is a plain name or a name with a length, a
     default, an alternate, an assign, an error, or a prefix or suffix trim. */
  fn apply_parameter_expansion(StringView spec) throws -> String;

  /* Compute the integer value of a $((...)) expression, resolving shell
     variables and applying any assignments inside it. */
  fn evaluate_arithmetic(StringView expression) throws -> i64;

  /* Turn a word into fields, applying tilde, variable expansion, command
     substitution, and IFS field splitting, but not globbing. */
  fn expand_word(const Word &word) throws -> ArrayList<glob_field>;

  fn expand_path_once(const glob_field &field, bool should_expand_files) throws
      -> ArrayList<glob_field>;
  fn expand_path_recurse(ArrayList<glob_field> fields) throws
      -> ArrayList<glob_field>;
  fn expand_path(glob_field field, SourceLocation location) throws
      -> ArrayList<String>;

  fn expand_tilde(WordSegment &leading_segment) const throws -> void;
};

/* Lower-level execution context. Path is the program path to execute, expanded
 * from program. Program is non-altered first arg. */
class ExecContext
{
public:
  static fn make_from(SourceLocation location,
                      ArrayList<String> &&args) throws -> ExecContext;

  /* Build directly from an already resolved builtin kind or program path,
     skipping the PATH search. A simple command memoizes its resolution and
     reuses it across the iterations of a loop. */
  static fn from_resolved(SourceLocation location, ResolvedCommand kind,
                          ArrayList<String> &&args) throws -> ExecContext;

  Maybe<os::descriptor> in_fd{};
  Maybe<os::descriptor> out_fd{};
  Maybe<os::descriptor> err_fd{};

  /* 2>&1 routes the standard error to wherever the standard output goes, and
     1>&2 the reverse. Applied after the file descriptors are placed. When both
     are present the source order decides the result, since each dup reads the
     current target of its source descriptor, so dup_out_to_err_came_last records
     which one the source wrote last. */
  bool dup_err_to_out{false};
  bool dup_out_to_err{false};
  bool dup_out_to_err_came_last{false};

  pure fn is_builtin() const wontthrow -> bool;

  pure fn args() const wontthrow -> const ArrayList<String> &;
  pure fn program() const wontthrow -> const String &;
  pure fn source_location() const wontthrow -> const SourceLocation &;

  fn close_fds() throws -> void;
  fn print_to_stdout(StringView s) const throws -> void;

  fn execute(bool is_async) throws -> i32;

  pure fn program_path() const wontthrow -> const Path &;
  pure fn builtin_kind() const wontthrow -> const Builtin::Kind &;

private:
  ExecContext(SourceLocation location, ResolvedCommand &&kind,
              ArrayList<String> &&args);

  ResolvedCommand m_kind;

  SourceLocation m_location;
  ArrayList<String> m_args{heap_allocator()};
};

/* Evaluate an arithmetic expression that holds only literals and operators, with
   no variable and no substitution, so the result is a compile-time constant. The
   analyze pass calls this to fold a constant $((...)) once instead of leaving the
   evaluator to re-parse it on every expansion. Returns None when the expression
   is not provably constant or fails to evaluate. */
fn try_fold_constant_arithmetic(StringView expression) wontthrow -> Maybe<i64>;

} /* namespace shit */
