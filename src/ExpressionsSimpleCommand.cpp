#include "Arena.hpp"
#include "Builtin.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "ExpressionsInternal.hpp"
#include "Lexer.hpp"
#include "Optimizer.hpp"
#include "Platform.hpp"
#include "Shitbox.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

namespace expressions {

AssignCommand::AssignCommand(SourceLocation location, const Assignment *a)
    : Command(location), m_assignment(a)
{}

AssignCommand::~AssignCommand() = default;

pure fn AssignCommand::assignment() const wontthrow -> const Assignment *
{
  return m_assignment;
}

fn AssignCommand::is_assignment() const wontthrow -> bool { return true; }

fn AssignCommand::as_assign_command() const wontthrow -> const AssignCommand *
{
  return this;
}

cold fn AssignCommand::analyze(AnalysisContext &actx,
                               bool is_unconditional) const throws -> void
{
  ASSERT(m_assignment != nullptr);

  /* A constant $((...)) on the right of x=$((2+2)) is folded once by the
     constant-arithmetic rule, so the loop body that re-runs this assignment
     reads the cached result. The rule reads the constant table, so the fold
     runs before the table records this assignment. */
  optimizer::optimize_node(this, actx);

  /* The constant-propagation rule records a name assigned a plain literal value
     in a straight-line block, so a later $name reference in a static condition
     or constant arithmetic reads the recorded value. The record is conservative
     in every uncertain case, where the table simply forgets the name. */
  let const &name = m_assignment->key();

  /* An element assignment a[i]=v or m[k]=v changes what $a or $((a)) reads
     without recording a scalar literal, so the base name before the bracket is
     forgotten rather than recorded under the bracketed key. */
  if (let const bracket = name.view().find_character('['); bracket.has_value())
  {
    LOG(All,
        "forgetting the constant for the array base '%.*s' after an element "
        "assignment",
        static_cast<int>(*bracket), name.view().data);
    actx.constant_variables.erase(name.view().substring_of_length(0, *bracket));
    return;
  }

  /* A plain scalar assignment inside a function body with no prior local for
     the name leaks the value to the global scope, the footgun shit's own
     default mood guards against at run time. The append form is left alone
     since it extends a value the name already holds. A name already assigned
     at the top level is an update of that global, not a new leaking binding,
     so the warning stays quiet for it. This is the shellcheck SC2030-style
     warning for a leaked variable. */
  if (actx.function_scope_depth > 0 && !m_assignment->is_append() &&
      !actx.function_local_names.contains(name.view()) &&
      !actx.global_assigned_names.contains(name.view()) &&
      !(actx.eval_context != nullptr &&
        actx.eval_context->get_variable_value(name.view()).has_value()))
  {
    actx.warn(source_location(),
              StringView{"This assignment to '"} + name +
                  "' in a function has no local, so the value leaks to the "
                  "global scope",
              "Declare it with local to keep it inside the function");
  }

  /* An unconditional top-level assignment records the name as an existing
     global, so a later function-body assignment to it reads as an update. */
  if (actx.function_scope_depth == 0 && is_unconditional &&
      !actx.has_seen_runtime_definer)
  {
    actx.global_assigned_names.add(name.view());
  }

  /* A conditional or nested assignment may not run, and a runtime definer such
     as eval or dot may already have changed the name out of view, so neither
     proves the value. The append form NAME+=VALUE depends on the prior value,
     which the prepass does not track. Each of these forgets the name rather
     than record it. */
  if (!is_unconditional || actx.has_seen_runtime_definer ||
      m_assignment->is_append())
  {
    LOG(All,
        "forgetting the constant for '%s', the assignment is conditional, "
        "appends, or follows a runtime definer",
        name.c_str());
    actx.constant_variables.erase(name.view());
    return;
  }

  let const literal = optimizer::literal_word_value(m_assignment->value_word());
  if (literal.has_value()) {
    LOG(All, "recording the constant '%s' = '%s'", name.c_str(),
        literal->c_str());
    actx.constant_variables.set(name.view(), literal->view());
    actx.optimizer_recorded_constants++;
    if (actx.should_trace_optimizer)
      actx.trace_optimizer_line(String{"recorded constant: "} + name + " = " +
                                *literal);
  } else {
    /* The value is only known at run time, so a constant recorded for this name
       under an earlier assignment no longer holds and is forgotten. */
    LOG(All,
        "forgetting the constant for '%s', its value is only known at run "
        "time",
        name.c_str());
    actx.constant_variables.erase(name.view());
  }
}

hot fn AssignCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_assignment != nullptr);

  LOG(All, "assigning the variable '%s'", m_assignment->key().c_str());

  cxt.set_current_location(source_location());

  /* The status the assignment leaves depends on the value. A value with no
     command substitution resets it to 0, while a command or function
     substitution in the value leaves the status of the last one, the way bash
     reports x=$(false) as 1 and a plain x=1 as 0. The status reset waits until
     after the expansion, so a $? in the value reads the prior command's status
     rather than a value this assignment cleared first. */
  let const value_ran_substitution =
      m_assignment->value_word().runs_substitution();

  /* The value expansion and the store throw a plain Error, an unset variable
     under set -u or a readonly name. The assignment has a source location, so
     the error is relocated to a caret at it the way process_args does for a
     command argument. An already located error from a deeper command
     substitution rides through, since it is a separate branch under ErrorBase.
   */
  try {
    let value = cxt.expand_word_for_assignment(m_assignment->value_word());

    /* a[i]=v and m[k]=v assign one array element. The evaluator routes the
       subscript to the indexed or the associative store. */
    const StringView key_view = m_assignment->key().view();
    if (let const bracket = key_view.find_character('[');
        bracket.has_value() && key_view[key_view.length - 1] == ']')
    {
      const StringView array_name = key_view.substring_of_length(0, *bracket);
      const StringView subscript = key_view.substring_of_length(
          *bracket + 1, key_view.length - *bracket - 2);
      cxt.assign_array_element(array_name, subscript, value.view(),
                               m_assignment->is_append());
      if (!value_ran_substitution) cxt.set_last_exit_status(0);
      return cxt.last_exit_status();
    }

    /* The append form NAME+=VALUE prepends the current value of NAME, treating
       an unset name as empty, so the store receives the concatenation. An
       integer name adds rather than concatenates, so the join wraps the
       appended expression for the arithmetic in the store. */
    if (m_assignment->is_append()) {
      let appended =
          String{cxt.get_variable_value(m_assignment->key()).value_or("")};
      if (cxt.is_integer_variable(m_assignment->key()))
        cxt.append_integer_expression(appended, value.view());
      else
        appended += value;
      value = steal(appended);
    }

    /* The assignment goes through set_shell_variable first, so it still rejects
       a readonly name and refreshes the cached IFS. Under allexport it is then
       marked for the environment so a child inherits it, while a later lookup
       still finds the shell copy. */
    cxt.set_shell_variable(m_assignment->key(), value);
    if (cxt.export_all()) {
      let const &key = m_assignment->key();
      cxt.record_environment_change(key);
      os::set_environment_variable(key, value);
      cxt.mark_exported(key);
    }
    if (!value_ran_substitution) cxt.set_last_exit_status(0);
    return cxt.last_exit_status();
  } catch (const Error &e) {
    throw relocate_error(e, source_location());
  }
}

cold fn AssignCommand::to_string() const throws -> String
{
  return "Assign " + m_assignment->to_ast_string();
}

cold fn AssignCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + "]";
}

SimpleCommand::SimpleCommand(SourceLocation location,
                             ArrayList<const Token *> &&args)
    : Command(location), m_args(steal(args))
{

  /* The command's location spans from its first word to the end of its last,
     so a caret under the whole command, such as a sourced '. file' in a
     backtrace, covers the argument and not only the command word. */
  if (!m_args.is_empty()) {
    const SourceLocation first = m_args[0]->source_location();
    const SourceLocation last = m_args.back()->source_location();
    m_location.position = first.position;
    m_location.length = last.position + last.length - first.position;
  }
}

SimpleCommand::~SimpleCommand() = default;

fn SimpleCommand::set_redirections(ArrayList<Redirection> &&redirections) throws
    -> void
{
  m_redirections = steal(redirections);
}

fn SimpleCommand::set_array_args(
    ArrayList<array_builtin_assignment> &&array_args) throws -> void
{
  m_array_args = steal(array_args);
}

namespace {

using expressions::Redirection;

/* Route an opened descriptor into one of the three standard slots a stage
   carries. A target fd of 0 is the standard input, 2 the standard error, and
   any other number the standard output, since a pipeline stage and an exec
   context only express those three. The last redirection of a descriptor wins,
   so a descriptor already in the slot closes before the new one takes its
   place. */
fn assign_standard_fd(Maybe<os::descriptor> &in_fd,
                      Maybe<os::descriptor> &out_fd,
                      Maybe<os::descriptor> &err_fd, i32 fd,
                      os::descriptor file_fd) throws -> void
{
  if (fd == 0) {
    if (in_fd) os::close_fd(*in_fd);
    in_fd = file_fd;
  } else if (fd == 2) {
    if (err_fd) os::close_fd(*err_fd);
    err_fd = file_fd;
  } else {
    if (out_fd) os::close_fd(*out_fd);
    out_fd = file_fd;
  }
}

/* A resolved duplication target, the descriptor or close marker in fd, or
   the csh both-streams filename when the bare >&word expanded to a name
   rather than a number, which bash reads as >word 2>&1. */
struct resolved_duplication
{
  i32 fd{-1};
  Maybe<String> both_streams_file{};
};

fn resolve_duplication(const Redirection &redir, EvalContext &cxt) throws
    -> resolved_duplication
{
  if (redir.target == nullptr)
    return resolved_duplication{redir.dup_fd, shit::None};

  ArrayList<const Token *> target_tokens{cxt.scratch_allocator()};
  target_tokens.push(redir.target);
  ArrayList<String> fields = cxt.process_args(target_tokens);
  if (fields.count() != 1) {
    throw ErrorWithLocation{redir.target->source_location(),
                            "Duplication target is not a single descriptor"};
  }

  String &field = fields[0];
  if (field == "-")
    return resolved_duplication{Redirection::DUP_FD_CLOSE, shit::None};

  let const parsed_descriptor = utils::parse_decimal_integer(field.view());
  if (parsed_descriptor.is_error() || parsed_descriptor.value() < 0) {
    if (redir.can_dup_be_filename)
      return resolved_duplication{-1, steal(field)};
    throw ErrorWithLocation{redir.target->source_location(),
                            "'" + field + "' is not a valid descriptor"};
  }
  return resolved_duplication{static_cast<i32>(parsed_descriptor.value()),
                              shit::None};
}

} // namespace

/* The file open mode a file redirection opens its target with. A plain > honors
   noclobber, >| overrides it, >> appends, <> opens read-write, and < and every
   other kind reads. */
static fn redirection_open_mode(Redirection::Kind kind,
                                bool no_clobber) wontthrow -> os::file_open_mode
{
  switch (kind) {
  case Redirection::Kind::TruncateOutput:
    return no_clobber ? os::file_open_mode::TruncateNoClobber
                      : os::file_open_mode::Truncate;
  case Redirection::Kind::TruncateOutputOverride:
    return os::file_open_mode::Truncate;
  case Redirection::Kind::AppendOutput: return os::file_open_mode::Append;
  case Redirection::Kind::ReadWrite: return os::file_open_mode::ReadWrite;
  default: return os::file_open_mode::Read;
  }
}

/* Resolve one redirection to an unplaced outcome, the shared open-and-stage
   work the three redirection sites repeat, so each keeps only its own
   descriptor placement. A heredoc or here-string body is staged to a temp
   file, a duplication settles to its source descriptor or its both-streams
   filename open, and a file target opens in the kind's mode. The descriptor
   returned is the caller's to place and to close. A failure throws a located
   error, and open_or_stage_failed, when given, is set true only for the open,
   stage, and ambiguous-target failures the simple-command path recovers from,
   so a duplication-resolve or a word-expansion error stays fatal. */
fn resolve_redirection(const Redirection &redir, EvalContext &cxt,
                       SourceLocation fallback_location,
                       bool *open_or_stage_failed,
                       bool allow_fd_memoization) throws -> resolved_redirection
{
  if (redir.kind == Redirection::Kind::Heredoc ||
      redir.kind == Redirection::Kind::HereString)
  {
    let body = String{};
    if (redir.kind == Redirection::Kind::Heredoc) {
      ASSERT(redir.heredoc_body != nullptr);
      body = redir.heredoc_body->clone();
      if (redir.should_expand_heredoc) body = cxt.expand_heredoc_body(body);
    } else {
      ASSERT(redir.target != nullptr);
      body = cxt.expand_word_for_assignment(
          static_cast<const tokens::WordToken *>(redir.target)->word());
      body += "\n";
    }

    let opened = os::write_to_temp_file(body);
    if (!opened) {
      if (open_or_stage_failed != nullptr) *open_or_stage_failed = true;
      throw ErrorWithLocation{redir.target != nullptr
                                  ? redir.target->source_location()
                                  : fallback_location,
                              "Could not stage the heredoc body: " +
                                  os::last_system_error_message()};
    }
    return resolved_redirection{redirection_outcome::Heredoc, redir.fd,
                                opened.take(), -1};
  }

  if (redir.kind == Redirection::Kind::DuplicateOutput ||
      redir.kind == Redirection::Kind::DuplicateInput)
  {
    let resolved_dup = resolve_duplication(redir, cxt);
    /* The both-streams filename opens like >file and points the standard error
       after it, the pair bash builds for the csh >&file spelling. */
    if (resolved_dup.both_streams_file.has_value()) {
      let opened = os::open_file_descriptor(
          *resolved_dup.both_streams_file,
          redirection_open_mode(Redirection::Kind::TruncateOutput,
                                cxt.no_clobber()));
      if (!opened) {
        if (open_or_stage_failed != nullptr) *open_or_stage_failed = true;
        throw ErrorWithLocation{redir.target->source_location(),
                                "Could not open '" +
                                    *resolved_dup.both_streams_file +
                                    "': " + os::last_system_error_message()};
      }
      return resolved_redirection{redirection_outcome::BothStreams, 1,
                                  opened.take(), -1};
    }
    return resolved_redirection{
        redirection_outcome::Duplicate, redir.fd, {}, resolved_dup.fd};
  }

  ASSERT(redir.target != nullptr);

  ArrayList<const Token *> target_tokens{cxt.scratch_allocator()};
  target_tokens.push(redir.target);
  /* The path is opened right here and the field is never stored, so it expands
     onto the scratch arena the command reclaims rather than the heap. */
  const ArrayList<String> target =
      cxt.process_args(target_tokens, /*args_are_transient=*/true);
  if (target.count() != 1) {
    if (open_or_stage_failed != nullptr) *open_or_stage_failed = true;
    throw ErrorWithLocation{redir.target->source_location(),
                            "Redirection target is not a single file"};
  }

  let mode = redirection_open_mode(redir.kind, cxt.no_clobber());

  const String &target_path = target[0];

  const bool should_memoize_append = allow_fd_memoization &&
                                     mode == os::file_open_mode::Append &&
                                     cxt.loop_depth() > 0;
  if (should_memoize_append) {
    let cached = cxt.find_loop_redirect_fd(redir.fd, target_path, mode);
    if (cached.has_value())
      return resolved_redirection{redirection_outcome::OpenedFile, redir.fd,
                                  cached.value(), -1, /*is_cached=*/true};
  }

  let opened = os::open_file_descriptor(target_path, mode);
  if (!opened) {
    if (open_or_stage_failed != nullptr) *open_or_stage_failed = true;
    throw ErrorWithLocation{redir.target->source_location(),
                            "Could not open '" + target_path +
                                "': " + os::last_system_error_message()};
  }

  let const file_fd = opened.take();
  if (should_memoize_append) {
    cxt.retain_loop_redirect_fd(redir.fd, target_path, mode, file_fd);
    return resolved_redirection{redirection_outcome::OpenedFile, redir.fd,
                                file_fd, -1, /*is_cached=*/true};
  }

  return resolved_redirection{redirection_outcome::OpenedFile, redir.fd,
                              file_fd, -1, /*is_cached=*/false};
}

fn SimpleCommand::redirect_exec_context(ExecContext &ec,
                                        EvalContext &cxt) const throws -> void
{
  LOG(Debug, "applying %zu redirections to the pipeline stage",
      m_redirections.count());
  for (let const &redir : m_redirections) {
    let const r = resolve_redirection(redir, cxt, source_location());
    switch (r.kind) {
    /* A heredoc or here-string fills the stage's standard input slot, the only
       slot a staged body can take. */
    case redirection_outcome::Heredoc:
      if (ec.in_fd) os::close_fd(*ec.in_fd);
      ec.in_fd = r.opened_fd;
      break;
    /* The both-streams filename takes the output slot and the standard error
       follows it through the cross-route flag. */
    case redirection_outcome::BothStreams:
      assign_standard_fd(ec.in_fd, ec.out_fd, ec.err_fd, 1, r.opened_fd);
      ec.dup_err_to_out = true;
      ec.dup_out_to_err_came_last = false;
      break;
    case redirection_outcome::OpenedFile:
      assign_standard_fd(ec.in_fd, ec.out_fd, ec.err_fd, r.target_fd,
                         r.opened_fd);
      break;
    case redirection_outcome::Duplicate:
      /* A self copy changes nothing. The standard cross-routing keeps the flag
         fast path. An arbitrary descriptor or the close form is not represented
         by the stage's three descriptor slots and is left to the compound path.
       */
      if (r.dup_from_fd == r.target_fd) {
      } else if (r.target_fd == 2 && r.dup_from_fd == 1) {
        ec.dup_err_to_out = true;
        ec.dup_out_to_err_came_last = false;
      } else if (r.target_fd == 1 && r.dup_from_fd == 2) {
        ec.dup_out_to_err = true;
        ec.dup_out_to_err_came_last = true;
      }
      break;
    }
  }
}

fn SimpleCommand::is_simple_command() const wontthrow -> bool { return true; }

pure fn SimpleCommand::args() const wontthrow
    -> const ArrayList<const Token *> &
{
  return m_args;
}

fn SimpleCommand::as_simple_command() const wontthrow -> const SimpleCommand *
{
  return this;
}

namespace {

/* Replace a command word that names an alias with the alias body. The body is
   split on whitespace, which covers the common case of an alias to a command
   and its options, and a name already expanded is not expanded again so a
   self-referential alias terminates. A quoted space inside the body is not
   preserved, since this expansion does not re-run the full tokenizer. */
fn expand_command_aliases(EvalContext &cxt, ArrayList<String> &args) throws
    -> void
{
  if (!cxt.has_aliases()) return;

  HashSet already_expanded{heap_allocator()};

  while (!args.is_empty()) {
    let const &word = args[0];

    if (already_expanded.contains(word.view())) break;

    let const body = cxt.get_alias(word);
    if (!body.has_value()) break;
    already_expanded.add(word.view());
    LOG(Debug, "expanding the alias '%s'", word.c_str());

    /* The alias body replaces the first word, so the split words go in front of
       the remaining arguments. ArrayList has no in-place erase, so the new list
       is built and swapped in. */
    let rebuilt = ArrayList<String>{};
    let current = String{};
    let const &body_value = *body;
    for (usize i = 0; i < body_value.count(); i++) {
      const char c = body_value[i];
      if (c == ' ' || c == '\t') {
        if (!current.is_empty()) {
          rebuilt.push(String{
              heap_allocator(), StringView{current.data(), current.count()}
          });
          current.clear();
        }
      } else {
        current += c;
      }
    }
    if (!current.is_empty())
      rebuilt.push(String{
          heap_allocator(), StringView{current.data(), current.count()}
      });

    for (usize i = 1; i < args.count(); i++)
      rebuilt.push(steal(args[i]));

    args = steal(rebuilt);
  }
}

/* Whether the command word is itself a glob pattern, an unquoted * or ? or a
   bracket expression in its literal text. The lone [ that opens a test command
   carries no closing ] in the same word and is left alone. A quoted segment
   never globs. */
static fn command_word_is_glob(const Word &word) wontthrow -> bool
{
  bool has_open_bracket = false;
  for (const WordSegment &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::UnquotedText) continue;
    for (usize i = 0; i < segment.text.count(); i++) {
      let const c = segment.text[i];
      if (c == '*' || c == '?') return true;
      if (c == '[') has_open_bracket = true;
      if (c == ']' && has_open_bracket) return true;
    }
  }
  return false;
}

} // namespace

hot fn SimpleCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  /* A command may have no words when it is only a redirection, such as > file,
     or only assignments, such as a=1 b=2 or a bare array assignment a=(1 2), so
     the redirections and the assignments still run below. */
  ASSERT(m_args.count() > 0 || !m_redirections.is_empty() ||
         m_local_vars.count() > 0 || !m_array_args.is_empty());

  /* Record where this command sits so a $LINENO in its words reports its line.
   */
  cxt.set_current_location(source_location());

  /* BASH_COMMAND reads the source text of the command running now. It is built
     only in the moods that expose the bash dynamic variables, so the posix mood
     pays nothing on the hot path. */
  if (cxt.bash_dynamic_variables_enabled())
    cxt.set_current_command(utils::merge_tokens_to_string(m_args));

  /* A glob in command position is rarely intended, the shit mood rejects it
     while a compatibility mood downgrades to a warning. The check reads the
     typed command word before its expansion, so a pattern that happens to match
     a single file is still caught. */
  if (!m_args.is_empty() && m_args[0]->kind() == Token::Kind::Word) {
    const Word &command_word =
        static_cast<const tokens::WordToken *>(m_args[0])->word();
    if (!m_command_word_is_glob.has_value())
      m_command_word_is_glob = command_word_is_glob(command_word);

    if (*m_command_word_is_glob) {
      let const location = m_args[0]->source_location();
      let const message =
          StringView{"A glob pattern in command position is rarely intended as "
                     "a command name"};
      let const note =
          StringView{"Quote it to run a literal name, or list the matches with "
                     "compgen -G"};
      if (cxt.mood() == mimic_mood::Default) {
        let error = ErrorWithLocation{location, message};
        error.set_note(note);
        throw error;
      }
      cxt.show_runtime_warning_at(location, message, note);
    }
  }

  if (cxt.should_echo()) {
    shit::print(utils::merge_tokens_to_string(m_args) + "\n");
    shit::flush();
  }

  /* The argument words are built on the scratch arena and reclaimed on every
     exit from this command, so a loop body does not accumulate a vector per
     iteration. A builtin that keeps a word past the command copies it into the
     heap-backed store, so nothing the release frees is still read. */
  let const args_mark = cxt.scratch_mark();
  defer { cxt.scratch_release(args_mark); };
  /* A <(...) or >(...) in the words below opens a pipe and forks a child or
     leaves a temp file. The mark is taken before the expansion so this command
     reaps only what it opens, leaving a substitution from an enclosing command,
     such as a while loop's producer, for that command to reap. */
  let const substitution_mark = cxt.mark_process_substitutions();
  let program_args = cxt.process_args(m_args, /*args_are_transient=*/true);
  defer { cxt.cleanup_process_substitutions(substitution_mark); };
  expand_command_aliases(cxt, program_args);

  LOG(Info, "dispatching the command '%s' with %zu words",
      program_args.is_empty() ? "" : program_args[0].c_str(),
      program_args.count());

  /* A bare exec, the word exec with no further argument, applies its
     redirections to the shell's own descriptors permanently rather than around
     a single command. A function named exec shadows the builtin and takes the
     ordinary path. */
  const Expression *command_word_function =
      (!program_args.is_empty() && cxt.has_functions())
          ? cxt.find_function(program_args[0])
          : nullptr;

  const bool is_bare_exec = program_args.count() == 1 &&
                            program_args[0] == "exec" &&
                            command_word_function == nullptr;

  /* Whether the command word resolves to a POSIX special builtin not shadowed
     by a function. It decides both that a redirection error exits the shell
     rather than failing the command, and that a prefix assignment persists, so
     it is computed once here and read on both paths. An empty command word, a
     bare redirection or assignment line, is not a special builtin. */
  const bool command_is_special_builtin =
      !program_args.is_empty() && command_word_function == nullptr &&
      is_special_builtin_name(program_args[0].view());

  /* A redirection takes effect even when the command expands to None, so > file
     with no command still creates the file. A heredoc on the standard input
     passes its staged descriptor to the exec context through this slot, and the
     guard closes it on any path that does not hand it off. A file or a
     cross-route on the standard output and error is staged in source order onto
     the real shell fd below rather than into a slot. */
  Maybe<os::descriptor> redirect_in_fd;
  bool was_redirect_in_fd_handed_off = false;
  /* The backups put the descriptors back once the command finishes, restored in
     reverse on every exit path. The standard fds are routed here in source
     order so a later 2>&1 copies the descriptor its source points at now rather
     than the one a deferred slot would place last. */
  ArrayList<os::saved_descriptor> dup_saved_descriptors{
      cxt.scratch_allocator()};
  defer
  {
    for (usize i = dup_saved_descriptors.count(); i > 0; i--)
      os::restore_descriptor(dup_saved_descriptors[i - 1]);
  };
  defer
  {
    if (!was_redirect_in_fd_handed_off && redirect_in_fd)
      os::close_fd(*redirect_in_fd);
  };

  /* Set just before a redirection resource failure throws, an open that failed
     or a descriptor that is not open, so the catch tells those apart from an
     expansion error in a target word, which must stay fatal. */
  bool did_redirection_open_fail = false;
  try {
    for (let const &redir : m_redirections) {
      let const r = resolve_redirection(redir, cxt, source_location(),
                                        &did_redirection_open_fail,
                                        /*allow_fd_memoization=*/!is_bare_exec);
      switch (r.kind) {
      case redirection_outcome::Heredoc: {
        const os::descriptor body_fd = r.opened_fd;
        /* A bare exec heredoc points the shell's standard input at the staged
           body for good and drops the temporary descriptor. Inside an
           in-process subshell the move is backed up first, so it stays
           contained the way a fork would contain it. */
        if (is_bare_exec) {
          cxt.snapshot_subshell_descriptor(redir.fd);
          shit::flush();
          os::replace_descriptor(redir.fd, body_fd);
#if SHIT_PLATFORM_IS WIN32
          /* A Windows descriptor is a HANDLE, so the staged body is compared
             against the handle that already occupies the shell slot rather than
             against the bare fd number. */
          if (body_fd != os::descriptor_for_shell_fd(redir.fd))
            os::close_fd(body_fd);
#else
          if (body_fd != redir.fd) os::close_fd(body_fd);
#endif
          break;
        }

        /* A heredoc on the standard input takes the in_fd slot. A numbered
           heredoc such as 3<<EOF targets descriptor N instead, which the three
           standard slots cannot express, so the body descriptor is staged onto
           the real shell fd N around the command and restored afterward, the
           same way a duplication onto an arbitrary descriptor is. */
        if (redir.fd == 0) {
          if (redirect_in_fd) os::close_fd(*redirect_in_fd);
          redirect_in_fd = body_fd;
          break;
        }

        /* The temp file already lands on fd N when mkstemp handed back that
           very number. A generic save then dup2 would leave the body open on N
           after the command, so the collision is handled directly and the
           restore closes fd N, which was free before mkstemp claimed it. */
#if SHIT_PLATFORM_IS WIN32
        const bool body_is_target_fd =
            body_fd == os::descriptor_for_shell_fd(redir.fd);
#else
        const bool body_is_target_fd = body_fd == redir.fd;
#endif
        if (body_is_target_fd) {
          dup_saved_descriptors.push(
              os::saved_descriptor{.shell_fd = redir.fd, .was_open = false});
          break;
        }
        dup_saved_descriptors.push(
            os::save_and_replace_descriptor(redir.fd, body_fd));
        os::close_fd(body_fd);
        break;
      }

      case redirection_outcome::BothStreams: {
        /* The both-streams filename lands on the standard output and the
           standard error follows it, the pair bash builds for the csh >&file
           spelling. The bare exec form keeps both moves for good the way its
           file redirect does. */
        const os::descriptor file_fd = r.opened_fd;
        shit::flush();
        if (is_bare_exec) {
          cxt.snapshot_subshell_descriptor(1);
          cxt.snapshot_subshell_descriptor(2);
          const bool out_ok = os::replace_descriptor(1, file_fd);
          const bool err_ok = os::replace_descriptor(2, file_fd);
          os::close_fd(file_fd);
          if (!out_ok || !err_ok) {
            did_redirection_open_fail = true;
            throw ErrorWithLocation{redir.target->source_location(),
                                    "Bad file descriptor"};
          }
          break;
        }
        const os::saved_descriptor saved_out =
            os::save_and_replace_descriptor(1, file_fd);
        dup_saved_descriptors.push(saved_out);
        const os::saved_descriptor saved_err =
            os::save_and_replace_descriptor(2, file_fd);
        dup_saved_descriptors.push(saved_err);
        os::close_fd(file_fd);
        if (!saved_out.is_dup2_ok || !saved_err.is_dup2_ok) {
          did_redirection_open_fail = true;
          throw ErrorWithLocation{redir.target->source_location(),
                                  "Bad file descriptor"};
        }
        break;
      }

      case redirection_outcome::Duplicate: {
        const i32 from_fd = r.dup_from_fd;

        /* A bare exec applies a duplication to the shell's own descriptor for
           good, so the copy or the close stays in effect for every later
           command, except inside an in-process subshell where the move is
           backed up and contained at the subshell's end. The flush keeps
           buffered output on the original descriptor before it moves. */
        if (is_bare_exec) {
          cxt.snapshot_subshell_descriptor(redir.fd);
          shit::flush();

          if (from_fd == Redirection::DUP_FD_CLOSE) {
            os::close_shell_fd(redir.fd);
            break;
          }

          /* A duplication onto a closed or invalid descriptor, as in exec 6>&9
             with fd 9 closed, fails the dup2. The exec fails with a located
             error and the shell keeps the descriptor unchanged, matching dash.
           */
          if (!os::replace_descriptor(redir.fd,
                                      os::descriptor_for_shell_fd(from_fd)))
          {
            const SourceLocation location =
                redir.target != nullptr ? redir.target->source_location()
                                        : source_location();
            did_redirection_open_fail = true;
            throw ErrorWithLocation{location, utils::int_to_text(from_fd) +
                                                  ": Bad file descriptor"};
          }
          break;
        }

        /* A descriptor copied onto itself, as in 1>&1, changes nothing. */
        if (from_fd == redir.fd) {
          break;
        }

        /* A cross-route between the standard output and error, as in 2>&1,
           points the real shell descriptor at the target in source order so a
           later file redirect on the source does not change what the copy
           already captured. The shell's buffered output is flushed first, so it
           lands on the original descriptor rather than the duplication target.
           The arbitrary descriptor and the close form take the same in-order
           path. */
        shit::flush();

        if (from_fd == Redirection::DUP_FD_CLOSE) {
          dup_saved_descriptors.push(os::save_and_replace_descriptor(
              redir.fd, os::descriptor_for_shell_fd(redir.fd)));
          os::close_fd(os::descriptor_for_shell_fd(redir.fd));
          break;
        }

        const os::saved_descriptor saved = os::save_and_replace_descriptor(
            redir.fd, os::descriptor_for_shell_fd(from_fd));
        dup_saved_descriptors.push(saved);
        /* A duplication onto a closed or invalid descriptor, as in >&5 with fd
           5 closed, fails the dup2. The command fails with a located error
           rather than writing to the original descriptor, matching dash. */
        if (!saved.is_dup2_ok) {
          const SourceLocation location = redir.target != nullptr
                                              ? redir.target->source_location()
                                              : source_location();
          did_redirection_open_fail = true;
          throw ErrorWithLocation{location, utils::int_to_text(from_fd) +
                                                ": Bad file descriptor"};
        }
        break;
      }

      case redirection_outcome::OpenedFile: {
        const os::descriptor file_fd = r.opened_fd;
        /* A bare exec points the shell's own descriptor at the opened file for
           good, then drops the temporary descriptor the open returned. The dup2
           onto fd N replaces whatever fd N held before, so a second exec onto
           the same number closes the earlier file rather than leaking it. The
           flush keeps buffered output on the original descriptor before it
           moves. */
        if (is_bare_exec) {
          cxt.snapshot_subshell_descriptor(redir.fd);
          shit::flush();
          const bool was_replaced = os::replace_descriptor(redir.fd, file_fd);
#if SHIT_PLATFORM_IS WIN32
          if (file_fd != os::descriptor_for_shell_fd(redir.fd))
            os::close_fd(file_fd);
#else
          if (file_fd != redir.fd) os::close_fd(file_fd);
#endif
          if (!was_replaced) {
            did_redirection_open_fail = true;
            throw ErrorWithLocation{redir.target->source_location(),
                                    utils::int_to_text(redir.fd) +
                                        ": Bad file descriptor"};
          }
          break;
        }

        /* Every file redirect is staged onto the real shell fd N in source
           order so a later 2>&1 copies the descriptor fd N points at now rather
           than the one the spawn would place last. A redirect onto fd 1 or fd 2
           mutates the shell's own standard output or error in place, so the
           buffered output is flushed first to land on the original descriptor.
         */
        if (redir.fd == 1 || redir.fd == 2) shit::flush();
#if SHIT_PLATFORM_IS WIN32
        const bool file_is_target_fd =
            file_fd == os::descriptor_for_shell_fd(redir.fd);
#else
        const bool file_is_target_fd = file_fd == redir.fd;
#endif
        if (file_is_target_fd) {
          /* open returned fd N itself, so the file already sits on its target.
             A generic save then dup2 then close would leave the child without
             it, so the collision is recorded for restore without a close. */
          dup_saved_descriptors.push(
              os::saved_descriptor{.shell_fd = redir.fd, .was_open = false});
        } else {
          dup_saved_descriptors.push(
              os::save_and_replace_descriptor(redir.fd, file_fd));
          if (!r.is_cached) os::close_fd(file_fd);
        }
        break;
      }
      }
    }
  } catch (const ErrorWithLocation &redirection_error) {
    /* An expansion error in a target word, such as ${x?msg} on an unset name or
       a division by zero, is fatal the way it is anywhere else, so only an open
       or dup failure is caught here. */
    if (!did_redirection_open_fail) throw;
    /* A redirection that cannot open its target, or names a closed descriptor,
       fails the command rather than the shell, the way dash continues past it.
       A special builtin is the exception, since its redirection error exits a
       non-interactive shell. The descriptor and heredoc defers above still put
       the partially applied redirections back. */
    if (command_is_special_builtin) throw;
    const String *source = cxt.current_source();
    show_message(redirection_error.to_string(source != nullptr ? source->view()
                                                               : StringView{}));
    /* bash reports a redirection failure with status 1 and dash with 2, the
       value a script reads in $? after the failed command. */
    let const redirection_status = cxt.is_bash_compatible() ? 1 : 2;
    cxt.set_last_exit_status(redirection_status);
    cxt.publish_single_pipe_status(redirection_status);
    return redirection_status;
  }

  /* An expansion may drop every word, for example an unset $x used as the whole
     command. The redirections above already took effect, and a command-less
     line still carries its assignments, which persist in the current shell
     rather than apply only to a child. */
  if (program_args.is_empty()) {
    for (let const &var : m_local_vars) {
      const StringView name = var.name.view();
      let value = cxt.expand_word_for_assignment(var.value);
      if (var.is_append) {
        let appended = String{cxt.scratch_allocator()};
        if (let const existing = cxt.get_variable_value(name))
          appended.append(existing->view());
        if (cxt.is_integer_variable(name)) {
          cxt.append_integer_expression(appended, value.view());
          char decimal[24];
          value = String{
              cxt.scratch_allocator(),
              utils::int_to_text_into(cxt.evaluate_arithmetic(appended.view()),
                                      decimal, sizeof(decimal))};
        } else {
          appended += value;
          value = steal(appended);
        }
      }
      cxt.set_shell_variable(name, value);
      if (cxt.export_all()) {
        cxt.record_environment_change(name);
        os::set_environment_variable(name, value.view());
        cxt.mark_exported(name);
      }
    }
    /* A command-less line may also carry bare array assignments, such as the
       pvars=() of flags= pvars=() specs=(), applied after the scalars in source
       order. */
    for (let const &assignment : m_array_args) {
      ArrayList<String> values = cxt.process_args(assignment.elements);
      cxt.assign_indexed_array_elements(assignment.name, steal(values),
                                        assignment.is_append);
    }
    /* A command word or an assignment value that ran a command substitution
       leaves the status of the last one, the way bash reports $(false) on its
       own line as 1 and a=$(false) b=$(true) as 0. A line with no
       substitution, a bare redirection or a plain assignment, resets to 0. */
    let do_token_ran_substitution = [&](const Token *token) {
      if (token == nullptr || token->kind() != Token::Kind::Word) return false;
      return static_cast<const tokens::WordToken *>(token)
          ->word()
          .runs_substitution();
    };
    let ran_substitution = false;
    for (let const token : m_args)
      ran_substitution = ran_substitution || do_token_ran_substitution(token);
    for (let const &var : m_local_vars)
      ran_substitution = ran_substitution || var.value.runs_substitution();
    for (let const &assignment : m_array_args)
      for (let const token : assignment.elements)
        ran_substitution = ran_substitution || do_token_ran_substitution(token);
    /* A bare assignment or redirection with no substitution is status zero and
       publishes a one-element PIPESTATUS. When a substitution ran, its own last
       command already set PIPESTATUS, so it is left in place. */
    if (!ran_substitution) {
      cxt.set_last_exit_status(0);
      cxt.publish_single_pipe_status(0);
    }
    return cxt.last_exit_status();
  }

  /* A prefix assignment before a special builtin persists after the command as
     a regular shell variable, the way POSIX keeps it, and commits to the store
     below rather than the process environment so it stays unexported. A
     per-command assignment otherwise applies to the environment for this
     command and a child or function sees it, with the previous value restored
     on every exit path. */
  struct saved_env_var
  {
    String name;
    Maybe<String> previous_value;
  };
  ArrayList<saved_env_var> saved_env{cxt.scratch_allocator()};
  /* A prefix IFS=... drives the shell's own word splitting for this command,
     read by the read builtin and by every expansion in the command, which take
     it from the live separator cache rather than the environment. The effective
     separators are saved before the first such prefix and restored on exit, the
     way the prefix PATH save below reverts the resolver. */
  bool ifs_was_assigned = false;
  String saved_ifs_separators{cxt.scratch_allocator()};
  /* The assignments apply left to right, each committed to the environment
     before the next is expanded, so a later value reads an earlier same-line
     one and a repeated name or a += accumulates. The previous environment
     values are saved for the restore on exit, which keeps the prefix temporary
     for this command. */
  for (let const &var : m_local_vars) {
    const StringView name = var.name.view();
    Maybe<String> previous = os::get_environment_variable(name);
    /* The value expansion throws a plain Error, an unset variable under set -u,
       so it is relocated to a caret at the command the prefix leads. */
    let expanded_value = String{};
    try {
      expanded_value = cxt.expand_word_for_assignment(var.value);
    } catch (const Error &e) {
      throw relocate_error(e, source_location());
    }
    /* The append form prepends the current value of NAME, read from the shell
       store before the environment so a non-exported shell variable still
       contributes. An integer name evaluates the join to its decimal here,
       since the environment write takes the value verbatim. */
    if (var.is_append) {
      let appended = String{cxt.scratch_allocator()};
      if (let const existing = cxt.get_variable_value(name))
        appended.append(existing->view());
      if (cxt.is_integer_variable(name)) {
        cxt.append_integer_expression(appended, expanded_value.view());
        char decimal[24];
        expanded_value = String{
            cxt.scratch_allocator(),
            utils::int_to_text_into(cxt.evaluate_arithmetic(appended.view()),
                                    decimal, sizeof(decimal))};
      } else {
        appended += expanded_value;
        expanded_value = steal(appended);
      }
    }

    /* A special builtin keeps the assignment outside the bash mood, so it
       commits to the store and leaves nothing for the defer to restore.
       set_shell_variable refreshes the IFS and PATH caches itself, so the
       temporary-cache bookkeeping below is skipped on this path. The bash mood
       drops the assignment after the command the way bash does, so it falls to
       the temporary path instead. */
    if (command_is_special_builtin && !cxt.is_bash_compatible()) {
      cxt.set_shell_variable(name, expanded_value);
      if (cxt.export_all()) {
        cxt.record_environment_change(name);
        os::set_environment_variable(name, expanded_value.view());
        cxt.mark_exported(name);
      }
      continue;
    }

    saved_env.push(saved_env_var{String{name}, steal(previous)});
    os::set_environment_variable(name, expanded_value.view());
    /* The prefix exports the name for the command, so the set carries it until
       the defer below rewinds the environment and the set with it. */
    cxt.mark_exported(name);
    /* A prefix PATH=... applies only to this command, so the resolver follows
       the temporary value while the command runs and reverts on exit. The
       resolver reads its own MAYBE_PATH rather than the environment, so a write
       to the environment alone would not change the search order. */
    if (name == "PATH")
      utils::set_path_for_resolution(String{expanded_value.view()});
    /* A prefix IFS=... re-aims the separator cache for the command's duration.
       The value before the first IFS prefix is saved once, so a name repeated
       on the line still reverts to where it began. */
    if (name == "IFS") {
      if (!ifs_was_assigned) {
        ifs_was_assigned = true;
        saved_ifs_separators =
            cxt.get_variable_value("IFS").value_or(String{" \t\n"});
      }
      cxt.set_field_separators(expanded_value.view());
    }
  }
  defer
  {
    /* The restore runs newest first, so a name spelled more than once on the
       line restores to the value it held before the first of its assignments
       rather than to an intermediate one. */
    bool path_was_assigned = false;
    for (usize i = saved_env.count(); i > 0; i--) {
      const saved_env_var &restore = saved_env[i - 1];
      if (restore.name == "PATH") path_was_assigned = true;
      if (restore.previous_value)
        os::set_environment_variable(restore.name.view(),
                                     restore.previous_value->view());
      else
        os::unset_environment_variable(restore.name.view());
      /* The exported set follows the rewind, so a name the prefix introduced
         leaves the set and a name it shadowed stays exported. */
      cxt.sync_exported_after_restore(restore.name.view(),
                                      restore.previous_value.has_value());
    }
    /* The prefix PATH reverts to the shell's effective PATH, the store value
       when set and the restored environment otherwise, so the next command
       resolves the way it would have without the prefix. */
    if (path_was_assigned)
      utils::set_path_for_resolution(cxt.get_variable_value("PATH"));
    /* The prefix IFS reverts to the separators that were effective before the
       command, so the next command splits the way it would have without it. */
    if (ifs_was_assigned) cxt.set_field_separators(saved_ifs_separators.view());
  };

  /* A function shadows a builtin and a program. Run its body with the call
     words as the positional parameters, restoring them afterwards. A return
     builtin unwinds here and supplies the function exit status. */
  ASSERT(!program_args.is_empty());
  let const &program_name = program_args[0];

  /* The command name is copied for the array-argument application below, since
     the argument vector is moved into the exec context before that point. */
  let array_command_name = String{};
  if (!m_array_args.is_empty())
    array_command_name = String{program_args[0].view()};

  if (const Expression *function_body =
          cxt.has_functions() ? cxt.find_function(program_name) : nullptr;
      function_body != nullptr)
  {
    /* A heredoc, a here-string, or an input file on the call lands on the
       real fd 0 for the body's duration, so the in-process body and every
       child it spawns read the staged bytes, the way bash redirects a forked
       function's stdin. The descriptor defer above restores the caller's
       stdin once the call ends. */
    if (redirect_in_fd) {
      dup_saved_descriptors.push(
          os::save_and_replace_descriptor(0, *redirect_in_fd));
      os::close_fd(*redirect_in_fd);
      redirect_in_fd = shit::None;
      was_redirect_in_fd_handed_off = true;
    }

    /* The caller's parameters are moved out and restored by moving back, so a
       deep copy of the list is not paid on every call. The store is empty in
       the window between, which the call-param build below does not read. */
    let saved_params = cxt.take_positional_params();
    let call_params = ArrayList<String>{};
    call_params.reserve(program_args.count() - 1);
    for (usize i = 1; i < program_args.count(); i++)
      call_params.push_managed(program_args[i]);
    cxt.set_positional_params(steal(call_params));
    defer { cxt.set_positional_params(steal(saved_params)); };

    /* Bound the call nesting so a function that recurses without a base case
       errors with a caret here rather than exhausting the native stack. The
       defer decrements on every unwind path. */
    cxt.enter_function_call(source_location());
    defer { cxt.leave_function_call(); };

    /* A loop in the caller is not the body's to break, so the body starts with
       a fresh loop count and the caller's count returns when the call ends. */
    let const saved_loop_depth = cxt.loop_depth();
    cxt.set_loop_depth(0);
    defer { cxt.set_loop_depth(saved_loop_depth); };

    /* The function body's transient scratch is reclaimed when the call returns,
       so a recursive function or a call in a loop does not grow the arena
       across frames. The status is an integer and the scope pop restores the
       locals into the heap-backed store, so nothing the release frees is still
       read. The release is registered first, so it runs last, after the scope
       pop. */
    let const call_mark = cxt.scratch_mark();
    defer { cxt.scratch_release(call_mark); };

    /* Open a local scope so a local builtin in the body shadows a variable and
       the old value returns when the call ends. The call name rides the same
       lifetime, the frame FUNCNAME reads. */
    cxt.enter_function_scope();
    cxt.push_function_call_name(program_name.view());
    defer
    {
      cxt.pop_function_call_name();
      cxt.leave_function_scope();
    };

    /* A command at the tail of the body must not exec the shell in place even
       when the call itself is the terminal command, since the call's cleanup,
       the positional restore and the scope pop, has to run after the body. */
    let const saved_terminal_exec = cxt.terminal_exec_allowed();
    cxt.set_terminal_exec_allowed(false);
    defer { cxt.set_terminal_exec_allowed(saved_terminal_exec); };

    /* The body runs in the mood and the diagnostics state the function was
       defined in, so a function defined in bash mood runs bash even after a
       later set --mood. The defining mood drives the strictness too, while an
       explicit set -u rides through apply_strictness_for_mood. The swap only
       happens when the defining state differs from the live state, so a
       matching call skips the capture, the defer, and the four setters
       entirely. */
    let const *const definition_info =
        cxt.function_definition_info_of(program_name.view());
    let const needs_state_swap =
        definition_info != nullptr &&
        (static_cast<mimic_mood>(definition_info->defining_mood) !=
             cxt.mood() ||
         definition_info->were_warnings_enabled_at_definition !=
             cxt.warnings_enabled() ||
         definition_info->were_diagnostics_disabled_at_definition !=
             cxt.diagnostics_disabled());
    Maybe<RuntimeState> saved_runtime_state = None;
    if (needs_state_swap)
      saved_runtime_state = cxt.enter_definition_state(*definition_info);
    defer
    {
      if (saved_runtime_state.has_value()) saved_runtime_state->restore(cxt);
    };

    /* A located error thrown from the body carries an absolute position into
       the file that defined the function, which the top-level handler cannot
       reach once this frame unwinds, so the error is rendered here while the
       stack still names the function. window_function_body_error rebases the
       position onto the definition copy and swaps the filename, and the error
       is marked rendered so the top-level handler keeps the status without
       printing it twice. An already-rendered error is rethrown untouched. */
    i64 function_ret = 0;
    try {
      function_ret = function_body->evaluate(cxt);
    } catch (ErrorWithLocationAndDetails &error) {
      if (!error.was_rendered())
        if (let const windowed = window_function_body_error(cxt, error);
            windowed.has_value())
        {
          show_message(error.to_string(*windowed));
          show_message(error.details_to_string(*windowed));
          error.set_rendered();
        }
      throw;
    } catch (ErrorWithLocation &error) {
      if (!error.was_rendered())
        if (let const windowed = window_function_body_error(cxt, error);
            windowed.has_value())
        {
          show_message(error.to_string(*windowed));
          error.set_rendered();
        }
      throw;
    }

    /* A return inside the body unwinds to here and supplies the status. A break
       or a continue is scoped to a loop inside this function, so it does not
       escape into a caller's loop and is consumed here. An exit is not ours and
       stays pending for the shell. */
    if (cxt.has_pending_control_flow()) {
      const control_flow::Kind kind = cxt.pending_control_flow().kind;
      if (kind == control_flow::Kind::Return) {
        function_ret = cxt.pending_control_flow().value;
        cxt.clear_control_flow();
      } else if (kind == control_flow::Kind::Break ||
                 kind == control_flow::Kind::Continue)
      {
        cxt.clear_control_flow();
      }
    }

    cxt.set_last_exit_status(static_cast<i32>(function_ret));
    return function_ret;
  }

  if (cxt.should_retitle_for_command())
    toiletline::set_title(utils::merge_args_to_string(program_args));

  /* Reuse a memoized resolution when the command word is unchanged, otherwise
     search PATH once and remember the result for the next run. */
  const bool is_cache_valid = m_resolved_kind.has_value() &&
                              program_args[0] == m_resolved_name &&
                              m_resolved_mood == cxt.mood();

  /* A command word that resolves to nothing is non-fatal. Report it to stderr,
     set status 127, and continue so the surrounding list, and-or chain, and
     command substitution proceed on the 127 the way a normal failing command
     drives them. The catch is narrow, so a real located error from elsewhere
     still aborts the command. */
  /* $_ reads the last argument of the previous command, so it is captured here
     before the argument vector moves into the exec context and set after the
     command runs. */
  let const last_argument =
      program_args.is_empty() ? String{} : program_args.back();

  Maybe<ExecContext> resolved_ec;
  try {
    resolved_ec =
        is_cache_valid
            ? ExecContext::from_resolved(source_location(), *m_resolved_kind,
                                         steal(program_args))
            : ExecContext::make_from(source_location(), steal(program_args),
                                     cxt.mood(), cxt.shitbox());
  } catch (const CommandNotFound &e) {
    report_command_not_found(cxt, e);
    cxt.set_last_exit_status(127);
    cxt.publish_single_pipe_status(127);
    return 127;
  }
  let ec = resolved_ec.take();

  if (!is_cache_valid) {
    /* A bare name that resolved to the shitbox builtin did so through the
       mood-gated fallback or the runtime shitbox toggle, both of which can
       differ between runs of this node, so that resolution is not memoized and
       the next run resolves afresh under the live mood. */
    Maybe<ResolvedCommand> to_cache;
    if (ec.is_builtin()) {
      if (ec.builtin_kind() != Builtin::Kind::Shitbox)
        to_cache = ResolvedCommand::from_builtin(ec.builtin_kind());
    } else {
      to_cache = ResolvedCommand::from_program(ec.program_path());
    }

    if (to_cache.has_value()) {
      m_resolved_kind = to_cache;
      /* The argument vector moved into the context, so the cached name reads
         from it there rather than from the now-emptied local. */
      m_resolved_name = ec.program();
      m_resolved_mood = cxt.mood();
    }
  }

  /* A heredoc on the standard input passes its staged descriptor through the
     in_fd slot, which the exec context now owns and closes. The standard output
     and error redirects already took effect in source order on the real shell
     fds above and are restored by the defer, so they need no slot here. */
  if (redirect_in_fd) ec.in_fd = redirect_in_fd;
  was_redirect_in_fd_handed_off = true;

  const i64 ret = utils::execute_context(steal(ec), cxt, is_async());
  cxt.set_last_argument(last_argument.view());

  /* An assignment builtin with NAME=(...) array arguments applies them after it
     runs, in the scope the builtin selects. local binds a local array, and a
     declare or typeset inside a function binds a local while one at the top
     level reaches the global store, where export marks the name for the
     environment. The builtin ran first, so a local outside a function has
     already errored and the elements never reach here. */
  if (!m_array_args.is_empty()) {
    let const is_local = array_command_name == "local";
    let const is_declare =
        array_command_name == "declare" || array_command_name == "typeset";
    let const is_function_local = is_declare && cxt.in_function_scope();
    let const is_export = array_command_name == "export";
    /* readonly NAME=(...), and declare -r NAME=(...), mark the array name so a
       later assignment to it is rejected the way bash refuses a readonly. The
       -r flag sits in the builtin's arguments, so it is read off them. */
    let is_readonly_request = array_command_name == "readonly";
    /* The -A flag declares an associative array, so local -A m=() and declare
       -A m=([k]=v) route to the string-keyed store rather than the indexed one.
     */
    let is_associative_request = false;
    if (is_declare || is_local)
      for (let const arg : m_args) {
        let const text = arg->raw_string();
        if (text.length() >= 2 && text.view()[0] == '-') {
          if (!is_readonly_request &&
              text.view().find_character('r').has_value())
          {
            is_readonly_request = true;
          }
          if (text.view().find_character('A').has_value())
            is_associative_request = true;
        }
      }
    for (let const &assignment : m_array_args) {
      if (is_local || is_function_local) cxt.declare_local(assignment.name);
      ArrayList<String> values = cxt.process_args(assignment.elements);
      if (is_associative_request) {
        /* The array is keyed by string, so the declaration registers the name
           and each [key]=value element fills one entry. A bare element with no
           bracketed key becomes a key with an empty value. */
        cxt.declare_associative_array(assignment.name);
        for (let const &element : values) {
          const StringView text = element.view();
          if (!text.is_empty() && text[0] == '[') {
            if (let const close = text.find_character(']');
                close.has_value() && *close + 1 < text.length &&
                text[*close + 1] == '=')
            {
              cxt.set_associative_element(
                  assignment.name, text.substring_of_length(1, *close - 1),
                  text.substring(*close + 2));
              continue;
            }
          }
          cxt.set_associative_element(assignment.name, text, StringView{});
        }
      } else {
        cxt.assign_indexed_array_elements(assignment.name, steal(values),
                                          assignment.is_append);
      }
      if (is_export) cxt.mark_exported(assignment.name);
      if (is_readonly_request) cxt.mark_readonly(assignment.name);
    }
  }

  cxt.set_last_exit_status(static_cast<i32>(ret));
  cxt.publish_single_pipe_status(static_cast<i32>(ret));
  return ret;
}

cold fn SimpleCommand::to_string() const throws -> String
{
  String s = "SimpleCommand";

  /* A pipeline stage that is a bare assignment carries the assignment in the
     local variables and has no command word, so the argument list is empty. */
  if (!m_args.is_empty()) {
    s += " \"" + m_args[0]->raw_string() + "\"";
    for (usize i = 1; i < m_args.count(); i++) {
      s += " \"";
      s += m_args[i]->raw_string();
      s += "\"";
    }
  }
  if (is_async()) s += ", Async";

  return s;
}

cold fn SimpleCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + "]";
}

} // namespace expressions

} // namespace shit
