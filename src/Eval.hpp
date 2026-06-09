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

/* One element of a [[ ]] conditional. An operand carries a word the evaluator
   expands without field splitting, and the rest are the operators the
   double-bracket grammar reads, the logical joiners, the parentheses, and the
   string-comparison angles. A word operator such as == or -f arrives as an
   operand and the evaluator classifies it by its text. */
struct conditional_element
{
  enum class Kind : u8
  {
    Operand,
    And,
    Or,
    Not,
    OpenParen,
    CloseParen,
    Less,
    Greater,
  };

  Kind kind;
  const Token *word{nullptr};
};

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
               const String *parent_source, String source_path)
      : origin(steal(origin)), call_site(call_site),
        parent_source(parent_source), source_path(steal(source_path))
  {}

  String origin;
  SourceLocation call_site;
  const String *parent_source;
  /* The bare path of a sourced file, the value BASH_SOURCE reports. It is empty
     for an eval or a sourced command with no backing file. */
  String source_path;
};

/* A variable binding saved when a local shadows it. The previous value is
   None when the name was unset, so leaving the scope restores the unset
   state rather than an empty string. The previous indexed array is saved the
   same way, so a local array such as local arr=(...) restores the caller's
   array on return and is cleared when the caller had none. */
struct local_binding
{
  String name;
  Maybe<String> previous_value;
  Maybe<ArrayList<String>> previous_indexed_array;
  /* The associative array the name held, as parallel key and value lists, with
     the flag set when the name was an associative array. A local -A restores the
     caller's map on return and clears it when the caller had none. */
  bool previous_was_associative{false};
  ArrayList<String> previous_associative_keys{heap_allocator()};
  ArrayList<String> previous_associative_values{heap_allocator()};
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

/* One reverted process-environment write, the prior value of a name an export,
   an unset, or a reassignment of an exported name changed inside a subshell.
   The value is absent when the name was unset before the write. */
struct environment_undo_entry
{
  String name;
  Maybe<String> previous_value;
};

/* A live process substitution. The shell keeps one pipe end open as the /dev/fd
   path the consuming command reads or writes, while the forked child runs the
   inner command on the other end. The descriptor is closed and the child reaped
   once the consuming command has finished. */
struct process_substitution
{
  os::descriptor shell_fd;
  os::process child;
  /* Where the command that opened the substitution sits, so a reap warning
     renders a caret under it. The source is the text that location indexes. It
     stays alive and unchanged through the command, so a view into it is held
     rather than the owning String. It is empty when there is no surrounding
     source. */
  SourceLocation location;
  StringView source;
};

/* A snapshot of the mutable shell state, taken around a subshell or a command
   substitution so a cd or an assignment inside does not leak to the parent. The
   set option flags and the trap table are captured too, so a set -e, a set -f,
   a set -x, or a trap inside the subshell stays inside it. */
struct eval_state_snapshot
{
  HashMap<String> shell_variables;
  HashMap<const Expression *> functions;
  HashMap<String> aliases;
  ArrayList<String> positional_params;
  Path working_directory;
  HashMap<String> traps;
  bool error_exit;
  bool enable_path_expansion;
  bool enable_echo;
  bool enable_echo_expanded;
  /* The length of the environment undo log when the snapshot was taken, the
     point restore_state rewinds the process environment back to. A subshell
     that writes no exported name leaves the log untouched, so the restore is a
     single length comparison. */
  usize environment_undo_mark;
};

/* Record a visit to a directory in the frecency store at
   ~/.shit_directory_history, for the z smart-cd builtin. Called after a
   successful cd. */
fn record_directory_access(StringView directory) throws -> void;

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

  /* The bash indexed arrays, a name to an ordered list of element strings. They
     live beside the scalar variables, and a scalar read of an array name yields
     element zero the way bash treats $a as ${a[0]}. */
  fn set_indexed_array(StringView name, ArrayList<String> values) throws
      -> void;
  fn append_indexed_array(StringView name, ArrayList<String> values) throws
      -> void;
  fn set_array_element(StringView name, usize index, StringView value) throws
      -> void;

  /* Assign one array element from a raw subscript, routing an associative name
     to a string key and an indexed name to an arithmetic index. The append form
     concatenates onto the current element. Used by the a[i]=v and m[k]=v
     parser path so the subscript expansion stays inside the evaluator. */
  fn assign_array_element(StringView name, StringView subscript,
                          StringView value, bool is_append) throws -> void;
  pure fn lookup_indexed_array(StringView name) const wontthrow
      -> const ArrayList<String> *
  {
    return m_indexed_arrays.find(name);
  }

  /* The bash associative arrays, a name to string-keyed values. The values are
     held in one flat map under a composite name-and-key, and the declared names
     are tracked separately so an empty associative array still routes a string
     subscript here rather than to the arithmetic indexed path. */
  fn declare_associative_array(StringView name) throws -> void;
  pure fn is_associative_array(StringView name) const wontthrow -> bool
  {
    return m_associative_names.contains(name);
  }
  fn set_associative_element(StringView name, StringView key,
                             StringView value) throws -> void;
  fn lookup_associative_element(StringView name, StringView key) const throws
      -> Maybe<String>;
  fn associative_keys(StringView name) const throws -> ArrayList<String>;
  fn associative_values(StringView name) const throws -> ArrayList<String>;
  /* Forget every element of an associative array and the name's membership, so a
     local -A leaving its scope drops its entries before the caller's are put
     back. */
  fn clear_associative_array(StringView name) throws -> void;

  /* Every element of an array name as a list, the indexed elements in order,
     the associative values in store order, or a one-element list for a plain
     scalar. A "${a[@]}" expansion emits one field per element from this, the
     way "$@" keeps each positional parameter its own field. An unset name
     yields an empty list. */
  fn collect_array_elements(StringView name) const throws -> ArrayList<String>;

  /* Log a name's current process-environment value before a write that outlives
     the current statement, so a subshell restore can revert it. Called before
     an export or an allexport assignment writes the environment. Outside a
     subshell the write is permanent, so nothing is logged. */
  fn record_environment_change(StringView name) throws -> void;

  /* The names that currently live in the process environment, the exported
     ones, mirrored so an assignment tests an O(1) set rather than scanning the
     environment on every write. mark_exported and unmark_exported keep it in
     step with each environment write, and the set is seeded from the inherited
     environment at construction. */
  fn mark_exported(StringView name) throws -> void;
  fn unmark_exported(StringView name) throws -> void;
  pure fn is_exported(StringView name) const wontthrow -> bool;

  /* Re-point the exported set after an environment value is restored, adding
     the name when the restore writes a value and removing it when the restore
     unsets, the way a subshell exit and a command-prefix teardown both rewind
     the environment. */
  fn sync_exported_after_restore(StringView name, bool has_value) throws
      -> void;

  /* Set IFS and refresh the separator table together, so the table never drifts
     from the cached value. A prefix IFS=... for a command applies it for the
     command's duration and restores it after, the way a prefix PATH=... re-aims
     the resolver. */
  fn set_field_separators(StringView value) throws -> void;
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

  /* Move the positional parameters out, leaving the store empty, so a function
     call saves the caller's parameters without a deep copy and restores them by
     moving the saved list back. */
  fn take_positional_params() wontthrow -> ArrayList<String>;

  fn set_last_exit_status(i32 status) wontthrow -> void;
  pure fn last_exit_status() const wontthrow -> i32;

  /* The wall-clock nanoseconds the last top-level command took, for the \D
     prompt segment. */
  fn set_last_command_duration_ns(u64 nanos) wontthrow -> void;
  pure fn last_command_duration_ns() const wontthrow -> u64;

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

  /* Poll the jobs, print a bash-style done line for every one that just
     finished, then forget those entries. The prompt loop calls this before each
     interactive prompt so a background job reports its completion the way bash
     reports it, and the caller gates the call on an interactive shell so a
     script stays silent. */
  fn notify_done_jobs() throws -> void;

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

  /* The names of every shell variable, so variable completion can offer them
     after a '$'. The environment names are added by the caller, since they live
     in the process rather than the store. */
  fn variable_names() const throws -> HashSet;

  /* trap stores an action to run for a condition, keyed by the condition name
     such as EXIT or INT. The EXIT action runs once when the shell ends. A
     signal condition also installs the shell's signal handler, so the action
     runs when the signal arrives. */
  fn set_trap(StringView condition, StringView action) throws -> void;
  fn remove_trap(StringView condition) throws -> void;
  pure fn traps() const wontthrow -> const HashMap<String> &;
  fn run_exit_trap() throws -> void;

  /* Run the action of every signal whose flag the handler set, called at the
     command boundary when os::SIGNAL_PENDING is set. A re-entrancy guard keeps
     a trap action that itself triggers a signal from nesting the drain. */
  fn run_pending_traps() throws -> void;
  /* True when an EXIT trap action is set, so the run loop keeps the fork for a
     terminal command and lets the trap run before the shell exits. */
  fn has_exit_trap() const wontthrow -> bool;

  /* A real shell process runs its EXIT trap as it exits, so a forked subshell
     runs its own EXIT trap at its end. This shell isolates a subshell by
     snapshot rather than a fork, so the boundary clears the inherited EXIT
     action on entry and fires whatever EXIT action the subshell set on exit.
     The clear keeps the inherited parent action from firing at the subshell's
     end, and the run happens before restore_state brings the parent table back.
     The global one-shot guard run_exit_trap uses is left untouched, so the
     parent still runs its own EXIT trap once when the shell ends. */
  fn clear_inherited_exit_trap() throws -> void;
  fn run_subshell_exit_trap() throws -> void;

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

  /* pipefail makes a pipeline report the status of the rightmost stage that
     failed rather than the last stage alone. */
  fn set_pipefail(bool enabled) wontthrow -> void;
  pure fn pipefail() const wontthrow -> bool;

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

  /* bash-compatible mode enables the bash extensions that change the meaning of
     valid POSIX syntax, such as the (( )) arithmetic command and brace
     expansion. The evaluator reads it for brace expansion and the parser is
     handed it at construction for the (( )) and C-style for syntax. */
  fn set_bash_compatible(bool enabled) wontthrow -> void
  {
    m_bash_compatible = enabled;
  }
  pure fn is_bash_compatible() const wontthrow -> bool
  {
    return m_bash_compatible;
  }

  /* POSIX mode behaves like dash. The non-posix-breaking bash additions that
     are on in the default mode too, such as the extended globs, read this to
     stay off only here. */
  fn set_posix_mode(bool enabled) wontthrow -> void { m_posix_mode = enabled; }
  pure fn is_posix_mode() const wontthrow -> bool { return m_posix_mode; }
  /* The extended globs are on everywhere except POSIX mode, the way bash treats
     a feature that POSIX rejects anyway as a pure addition. */
  pure fn extglob_enabled() const wontthrow -> bool { return !m_posix_mode; }

  /* The bash shopt option states, set and read by the shopt builtin. A name
     with no entry reads as off. */
  fn set_shopt_option(StringView name, bool enabled) throws -> void
  {
    m_shopt_options.set(name, enabled);
  }
  pure fn is_shopt_enabled(StringView name) const wontthrow -> bool
  {
    const bool *value = m_shopt_options.find(name);
    return value != nullptr && *value;
  }
  pure fn shopt_options() const wontthrow -> const HashMap<bool> &
  {
    return m_shopt_options;
  }

  /* A condition, such as an if test or an && operand, suppresses set -e while
     it runs, since its failure is expected. */
  fn enter_condition() wontthrow -> void;
  fn leave_condition() wontthrow -> void;
  pure fn in_condition() const wontthrow -> bool;

  /* The count of loops currently running in the active execution context. A
     loop body increments it around the iterations and decrements after. The
     break and continue builtins clamp their requested level to this count, so a
     level past the nesting breaks every enclosing loop rather than escaping as
     an error, and a request with no enclosing loop is dropped. A function call
     and a subshell save and zero this around their body, so a jump inside them
     sees only their own loops, matching dash. */
  fn enter_loop() wontthrow -> void;
  fn leave_loop() wontthrow -> void;
  pure fn loop_depth() const wontthrow -> usize;
  fn set_loop_depth(usize depth) wontthrow -> void;

  /* The run loop sets this before the final chunk's evaluation when the shell
     will exit with that chunk's status and no EXIT trap is pending. A terminal
     external command then replaces the shell process instead of fork, exec, and
     wait, the way dash execs the last command under EV_EXIT. The flag rides
     only the tail position. A compound list clears it on every node but its
     last, and every other node clears it, so only a command whose status
     becomes the shell's status sees it set. */
  fn set_terminal_exec_allowed(bool enabled) wontthrow -> void;
  pure fn terminal_exec_allowed() const wontthrow -> bool;

  /* The name=value lines that set with no argument prints, sorted. */
  fn sorted_variable_assignments() const throws -> ArrayList<String>;

  /* Expand a word in assignment context, with variable, tilde, and command
     substitution but no field splitting and no globbing. */
  fn expand_word_for_assignment(const Word &word) throws -> String;

  /* Evaluate a [[ ]] conditional element list and report whether it is true.
     The operands expand without field splitting, == and != glob match their
     right side, < and > compare strings, and && and || join primaries. */
  /* Compute the integer value of a $((...)) or (( )) expression, resolving
     shell variables and applying any assignments inside it. */
  fn evaluate_arithmetic(StringView expression) throws -> i64;

  fn evaluate_conditional(const ArrayList<conditional_element> &elements) throws
      -> bool;

  /* Expand a case pattern word the same way assignment context expands, with no
     field splitting and no pathname globbing, plus a parallel mask that marks
     which output bytes may act as glob metacharacters. A byte from a quoted or
     double-quoted region is inactive, an unquoted literal byte is active, and a
     byte from an unquoted expansion is active so a * or ? in the value matches
     as a wildcard. The case matcher reads the mask so a quoted metacharacter in
     the pattern matches literally. */
  fn expand_case_pattern_masked(const Word &word,
                                ArrayList<bool> &active_out) throws -> String;

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

  /* Run a <(...) or >(...) process substitution. The text leads with the
     direction byte. A pipe is opened, the inner command runs in a forked child
     on one end, and the shell keeps the other end open and returns its /dev/fd
     path. The descriptor and the child are recorded so the command that uses
     the path can clean them up when it finishes. */
  fn setup_process_substitution(StringView text) throws -> String;
  /* Close the descriptors and reap the children of the process substitutions a
     command opened. Closing first sends SIGPIPE to a producer that has more to
     write, so it ends rather than blocking the reap. */
  fn cleanup_process_substitutions() wontthrow -> void;

  /* Run a parsed inner command under the substitution machinery, capturing its
     stdout and snapshotting state so a cd or an assignment inside does not
     leak. Both capture overloads share this once they hold an AST. */
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

  /* The same expansion, plus a parallel mask that marks which output bytes may
     act as glob metacharacters. A byte from a quoted, single-quoted, or escaped
     region is inactive, an unquoted literal byte is active, and a byte from an
     unquoted expansion is active so a * or ? in the value globs. The ${x#pat}
     and ${x%pat} forms read the mask so a quoted * in the pattern matches
     literally rather than as a wildcard. */
  fn expand_modifier_word_masked(StringView word, ArrayList<bool> &active_out,
                                 bool remove_quotes = true) throws -> String;

  /* The shared body behind the two public forms. is_pattern_word makes a
     backslash quote the following byte and mark it inactive, the # and % rule,
     while the plain form keeps a backslash that precedes an ordinary byte. */
  fn expand_modifier_word_worker(StringView word, ArrayList<bool> &active_out,
                                 bool remove_quotes,
                                 bool is_pattern_word) throws -> String;

  pure fn should_echo() const wontthrow -> bool;
  pure fn should_echo_expanded() const wontthrow -> bool;
  pure fn shell_is_interactive() const wontthrow -> bool;

  /* False until the startup profile and rc files finish sourcing, so the
     per-command terminal title is not set for the commands those files run, the
     way bash leaves the title alone while reading its startup files. */
  pure fn startup_finished() const wontthrow -> bool
  {
    return m_startup_finished;
  }
  fn set_startup_finished() wontthrow -> void { m_startup_finished = true; }

  fn make_stats_string() const throws -> String;

  /* Stats counting is off unless -S asked for the report. The evaluate path
     tests this so the per-node bookkeeping never runs when nobody reads it. */
  fn set_stats_enabled(bool enabled) wontthrow -> void;
  pure fn stats_enabled() const wontthrow -> bool;

  /* The shell's own debug toggles, seeded from the -A, -M, and -E flags and
     flipped at runtime by set. The run loop reads the context rather than the
     flags so set -A turns the dump on for the next command. */
  fn set_show_ast(bool enabled) wontthrow -> void { m_show_ast = enabled; }
  pure fn show_ast() const wontthrow -> bool { return m_show_ast; }
  fn set_show_lexed_words(bool enabled) wontthrow -> void
  {
    m_show_lexed_words = enabled;
  }
  pure fn show_lexed_words() const wontthrow -> bool
  {
    return m_show_lexed_words;
  }
  fn set_show_exit_code(bool enabled) wontthrow -> void
  {
    m_show_exit_code = enabled;
  }
  pure fn show_exit_code() const wontthrow -> bool { return m_show_exit_code; }

  /* The granular memory report at exit, requested by --show-memory and read by
     quit through the context pointer rather than a separate global. */
  fn set_memory_stats_enabled(bool enabled) wontthrow -> void
  {
    m_memory_stats_enabled = enabled;
  }
  pure fn memory_stats_enabled() const wontthrow -> bool
  {
    return m_memory_stats_enabled;
  }

  pure fn last_expressions_executed() const wontthrow -> usize;
  pure fn total_expressions_executed() const wontthrow -> usize;

  pure fn last_expansion_count() const wontthrow -> usize;
  pure fn total_expansion_count() const wontthrow -> usize;

  pure fn commands_evaluated() const wontthrow -> usize;
  pure fn peak_ast_arena_bytes() const wontthrow -> usize;

protected:
  bool m_stats_enabled{false};
  bool m_show_ast{false};
  bool m_show_lexed_words{false};
  bool m_show_exit_code{false};
  bool m_memory_stats_enabled{false};
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
  HashMap<ArrayList<String>> m_indexed_arrays{heap_allocator()};
  HashSet m_associative_names{heap_allocator()};
  HashMap<String> m_associative_values{heap_allocator()};
  HashMap<bool> m_shopt_options{heap_allocator()};
  /* The cached value of IFS, kept current by set_shell_variable, so word
     splitting does not look it up in the map or the environment per word. */
  String m_field_separators{" \t\n"};

  /* A byte-indexed table that answers whether a character is a field separator
     in one load, instead of scanning IFS per byte. The layout is a contiguous
     256-byte block, the shape a later SIMD scan reads to find separators in
     bulk. It is rebuilt whenever IFS changes. */
  bool m_field_separator_table[256]{};
  pure fn is_field_separator(char c) const wontthrow -> bool;
  i32 m_last_exit_status{0};

  u64 m_last_command_duration_ns{0};

  String m_shell_name{};
  ArrayList<String> m_positional_params{heap_allocator()};
  Maybe<i64> m_last_background_pid{};
  HashMap<const Expression *> m_functions{heap_allocator()};
  usize m_subshell_depth{0};
  usize m_condition_depth{0};
  usize m_loop_depth{0};

  /* The prior values of process-environment names written while a subshell ran,
     pushed newest last and rewound by restore_state on the subshell's exit. The
     log is appended to only while m_subshell_depth is above zero, so a
     top-level export pays nothing and the common command substitution that
     writes no exported name leaves it empty. */
  ArrayList<environment_undo_entry> m_environment_undo_log{heap_allocator()};
  /* The names currently in the process environment, kept in step with every
     environment write so an assignment tests membership in O(1) rather than
     scanning the environment. */
  HashSet m_exported_names{heap_allocator()};
  ArrayList<process_substitution> m_pending_process_substitutions{
      heap_allocator()};

  /* The nesting depth of dot-source and eval runs, and of function calls, each
     bounded so a runaway recursion errors with a located message rather than
     growing the native stack and the arena until the process is killed. A
     pathological autoconf self-test that regenerates and re-sources a file
     forever is the motivating case. */
  usize m_source_depth{0};
  usize m_function_call_depth{0};

  /* Set once the startup files finish, so the per-command title is quiet while
     they run. */
  bool m_startup_finished{false};

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
  bool m_pipefail{false};
  bool m_no_clobber{false};
  bool m_export_all{false};
  bool m_no_exec{false};
  bool m_bash_compatible{false};
  bool m_posix_mode{false};
  /* The unix time the shell started, the base $SECONDS counts from. */
  i64 m_shell_start_time{0};
  /* Whether the $RANDOM generator has been seeded, set on the first read so a
     run that never reads RANDOM pays neither the seed nor its syscall. */
  mutable bool m_random_seeded{false};
  bool m_failglob{true};
  usize m_getopts_char_index{1};
  i64 m_getopts_last_optind{0};
  HashMap<String> m_traps{heap_allocator()};
  bool m_exit_trap_ran{false};
  /* True while run_pending_traps is draining, so a signal delivered during a
     trap action does not nest a second drain. */
  bool m_running_traps{false};
  bool m_terminal_exec_allowed{false};

  /* Install the OS signal disposition for each signal in the trap table, called
     after a subshell restore brings the parent's table back so the process
     dispositions match it. */
  fn install_trap_dispositions() throws -> void;

  /* The names marked read-only, checked by set_shell_variable on every
     assignment, so a set gives the membership test in O(1). */
  HashSet m_readonly_names{heap_allocator()};
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

  /* Remove a variable without the read-only check, for the same local restore
     path as assign_variable where a throw would terminate the shell. */
  fn force_unset_shell_variable(StringView name) throws -> void;

  /* Expand a ${...} body, which is a plain name or a name with a length, a
     default, an alternate, an assign, an error, or a prefix or suffix trim. */
  fn apply_parameter_expansion(StringView spec) throws -> String;

  /* Expand the bash substring form ${name:offset:length}. The body is the text
     after the first colon, an arithmetic offset and an optional arithmetic
     length. A negative offset counts from the end, and a negative length leaves
     that many characters off the end. */
  fn apply_substring_expansion(StringView name, StringView body) throws
      -> String;

  /* Expand the bash pattern-replacement forms ${name/pat/rep},
     ${name//pat/rep}, ${name/#pat/rep}, and ${name/%pat/rep}. The spec begins
     at the slash. The pattern is a glob, the replacement is a literal word, and
     a leading second slash replaces every match while # and % anchor the
     pattern to the start or the end. */
  fn apply_pattern_replacement(StringView name, StringView spec) throws
      -> String;

  /* Expand the bash case-modification forms ${name^}, ${name^^}, ${name,}, and
     ${name,,}. The ^ raises and the , lowers, a single operator touches the
     first character and a doubled one touches every character, and an optional
     glob after the operator limits which characters are affected. */
  fn apply_case_modification(StringView name, StringView spec) throws -> String;

  /* Expand the bash array element reference ${name[subscript]}. A subscript of
     @ or * yields every element, an arithmetic subscript yields one, and a
     negative index counts from the end. */
  fn apply_array_subscript(StringView name, StringView subscript) throws
      -> String;

  /* Expand the bash ${!body} form. When body ends with * or @ it lists the
     variable names that start with the prefix, sorted and space joined.
     Otherwise it is indirect, body names a variable whose value names the
     variable to expand. */
  fn apply_indirect_or_name_listing(StringView body) throws -> String;

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
  static fn make_from(SourceLocation location, ArrayList<String> &&args) throws
      -> ExecContext;

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
     current target of its source descriptor, so dup_out_to_err_came_last
     records which one the source wrote last. */
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

  /* Apply the 2>&1 and 1>&2 cross-routing in the order the source wrote them.
     Each duplication reads the current target of its source descriptor, so when
     both are present the one that came last in the source must run last. The
     two callables carry the platform's own way to point one descriptor at the
     other, a posix_spawn file action, a dup2, or a Windows handle assignment.
     Apply_err_to_out points the standard error at the standard output for 2>&1,
     and apply_out_to_err the reverse for 1>&2. */
  template <typename ApplyErrToOut, typename ApplyOutToErr>
  fn apply_dup_routing(ApplyErrToOut apply_err_to_out,
                       ApplyOutToErr apply_out_to_err) const -> void
  {
    if (dup_err_to_out && dup_out_to_err) {
      if (dup_out_to_err_came_last) {
        apply_err_to_out();
        apply_out_to_err();
      } else {
        apply_out_to_err();
        apply_err_to_out();
      }
    } else if (dup_err_to_out) {
      apply_err_to_out();
    } else if (dup_out_to_err) {
      apply_out_to_err();
    }
  }

private:
  ExecContext(SourceLocation location, ResolvedCommand &&kind,
              ArrayList<String> &&args);

  ResolvedCommand m_kind;

  SourceLocation m_location;
  ArrayList<String> m_args{heap_allocator()};
};

/* Parse and evaluate a constant arithmetic expression with no evaluation
   context. The optimizer's constant fold calls this once the byte scan proves
   the source holds no variable and no substitution, so the parser never
   dereferences a context. A malformed constant, such as a division by zero,
   throws. This is the Eval-side primitive the optimizer reaches, since the
   arithmetic parser lives in this translation unit. */
fn evaluate_constant_arithmetic(StringView expression) throws -> i64;

} /* namespace shit */
