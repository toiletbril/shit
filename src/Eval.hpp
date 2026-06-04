#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "Containers.hpp"
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
  /* The loop level for break and continue, or the status for return and exit. */
  i64 value{0};
  /* Where the requesting builtin sits, so an escaped jump points a caret at it.
     The source the offset indexes lives in source, set when the jump is made. */
  SourceLocation location{0, 0};
  const std::string *source{nullptr};
  std::string origin{};
};

/* A variable binding saved when a local shadows it. The previous value is
   nothing when the name was unset, so leaving the scope restores the unset
   state rather than an empty string. */
struct LocalBinding
{
  std::string name;
  Maybe<std::string> previous_value;
};

/* A snapshot of the mutable shell state, taken around a subshell or a command
   substitution so a cd or an assignment inside does not leak to the parent. */
struct EvalStateSnapshot
{
  HashMap<String> shell_variables;
  HashMap<const Expression *> functions;
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
  std::vector<std::string> process_args(const ArrayList<const Token *> &args);

  /* The allocator for transient expansion data, which a bump arena hands out
     and reclaims whole when the command ends. */
  Allocator
  scratch_allocator()
  {
    return bump_allocator(m_scratch_arena);
  }
  /* Reclaim the scratch arena, called between top-level commands. */
  void
  reset_scratch_arena()
  {
    m_scratch_arena.reset();
  }

  void set_shell_variable(const std::string &name, std::string value);
  void unset_shell_variable(const std::string &name);
  Maybe<std::string> get_variable_value(const std::string &name) const;

  /* The stored value of a plain shell variable, or nullptr when the name is
     unset or names a special parameter. The pointer reads the value without a
     copy and stays valid until the next assignment to that name. */
  const String *
  lookup_shell_variable(StringView name) const
  {
    return m_shell_variables.find(name);
  }

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
  const HashMap<String> &traps() const;
  void run_exit_trap();

  /* readonly marks a variable so a later assignment to it fails. The set is
     usually empty, so set_shell_variable only scans it when it is not. */
  void mark_readonly(const std::string &name);
  bool is_readonly(const std::string &name) const;
  std::vector<std::string> readonly_names() const;

  /* A function call pushes a local scope so a local builtin inside it can
     shadow a variable and have the old value restored when the call returns.
     declare_local records the current binding of a name in the innermost scope
     before the caller overwrites it. */
  void enter_function_scope();
  void leave_function_scope();
  bool in_function_scope() const;
  void declare_local(const std::string &name);

  /* alias maps a command word to its replacement text, consulted by the parser
     before a simple command. */
  void set_alias(const std::string &name, const std::string &value);
  bool remove_alias(const std::string &name);
  Maybe<std::string> get_alias(const std::string &name) const;
  std::vector<std::string> alias_definitions() const;
  /* The defined alias names, so the prepass treats a use of one as resolvable. */
  std::unordered_set<std::string> alias_names() const;

  /* Save and restore the mutable state around a subshell or a command
     substitution, so changes inside do not leak to the parent. */
  EvalStateSnapshot snapshot_state() const;
  void restore_state(EvalStateSnapshot snapshot);

  /* Track whether evaluation is inside a subshell or a command substitution, so
     the exit builtin ends only that scope instead of the whole shell. */
  void enter_subshell();
  void leave_subshell();
  bool in_subshell() const;

  /* The control-flow channel the break, continue, return, and exit builtins
     write instead of throwing. A request records where it was made against the
     current source, so an escaped jump points a caret at the builtin. The
     tree-walk checks has_pending_control_flow after each child and consumes the
     request at the matching loop, function, or subshell boundary. */
  void request_break(i64 level, SourceLocation location);
  void request_continue(i64 level, SourceLocation location);
  void request_return(i64 status, SourceLocation location);
  void request_exit(i64 status, SourceLocation location);
  bool has_pending_control_flow() const;
  ControlFlow &pending_control_flow();
  const ControlFlow &pending_control_flow() const;
  void clear_control_flow();

  /* The source currently being evaluated and its human name, so a control-flow
     request or a deferred report formats a caret against the right text. Set
     around a top-level run and around a sourced run, restoring the previous
     frame afterwards. */
  void set_current_source(const std::string *source, std::string origin);
  const std::string *current_source() const;
  const std::string &current_origin() const;

  /* The set builtin toggles these options at run time. error_exit is set -e,
     echo_expanded is set -x, and error_unset is set -u. */
  void set_error_exit(bool enabled);
  bool error_exit() const;
  void set_echo_expanded(bool enabled);
  void set_error_unset(bool enabled);
  bool error_unset() const;

  /* noclobber rejects an overwrite of an existing file through a plain >, set by
     -C and set -o noclobber. */
  void set_no_clobber(bool enabled);
  bool no_clobber() const;
  /* allexport marks every later assignment for the environment, set by -a and
     set -o allexport. */
  void set_export_all(bool enabled);
  bool export_all() const;
  /* noglob disables pathname expansion, set by -f and set -o noglob, and shares
     the path-expansion flag the field splitting already reads. */
  void set_no_glob(bool enabled);
  bool no_glob() const;
  /* noexec parses without running, set by -n and set -o noexec. */
  void set_no_exec(bool enabled);
  bool no_exec() const;

  /* A condition, such as an if test or an && operand, suppresses set -e while
     it runs, since its failure is expected. */
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

  /* Keep a top-level command's tree alive past its run, so a function it
     defined stays callable on the next command. The retained trees are
     destroyed by clear_retained_sources while the arena is still valid. */
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

  BumpArena m_scratch_arena{};
  HashMap<String> m_shell_variables{heap_allocator()};
  /* The cached value of IFS, kept current by set_shell_variable, so word
     splitting does not look it up in the map or the environment per word. */
  std::string m_field_separators{" \t\n"};
  i32 m_last_exit_status{0};

  std::string m_shell_name{};
  std::vector<std::string> m_positional_params{};
  Maybe<i64> m_last_background_pid{};
  HashMap<const Expression *> m_functions{heap_allocator()};
  usize m_subshell_depth{0};
  usize m_condition_depth{0};

  /* The pending non-local jump, Normal when none is pending. */
  ControlFlow m_control_flow{};
  /* The source and name of the text being evaluated, for caret formatting. */
  const std::string *m_current_source{nullptr};
  std::string m_current_origin{};

  /* ASTs from eval and dot, kept alive until the next top-level command so a
     function they define survives the rest of the current one. */
  ArrayList<Expression *> m_retained_source_asts{heap_allocator()};

  /* The source text of each eval and dot run, kept alive for as long as the
     ASTs above. A control-flow jump made inside one points its caret at this
     text, so the pointer must outlive the run that escaped it. */
  ArrayList<std::string *> m_retained_sources{heap_allocator()};

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
  std::vector<std::vector<LocalBinding>> m_local_scopes{};
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
  ArrayList<GlobField> expand_word(const Word &word);

  ArrayList<GlobField> expand_path_once(const GlobField &field,
                                        bool should_expand_files);
  ArrayList<GlobField> expand_path_recurse(ArrayList<GlobField> fields);
  ArrayList<String> expand_path(GlobField field);

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
