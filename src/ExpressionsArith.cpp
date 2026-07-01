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

ConditionalCommand::ConditionalCommand(SourceLocation location,
                                       ArrayList<conditional_element> elements)
    : CompoundCommand(location), m_elements(steal(elements))
{}

ConditionalCommand::~ConditionalCommand() = default;

cold fn ConditionalCommand::to_string() const throws -> String
{
  return "ConditionalCommand";
}

cold fn ConditionalCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + "]";
}

fn ConditionalCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  /* The [[ ]] evaluator reports a malformed expression as a plain error,
     relocated to the whole [[ ]] span so the caret covers the construct. An
     already-located diagnostic passes through untouched. The current location
     moves here first so a runtime warning from the operand expansion carets
     this [[ rather than the statement before it. */
  cxt.set_current_location(source_location());
  i64 status;
  try {
    status = cxt.evaluate_conditional(m_elements) ? 0 : 1;
  } catch (const Error &e) {
    SourceLocation span = source_location();
    if (source_end_position() > span.position)
      span.length = source_end_position() - span.position;
    throw relocate_error(e, span);
  }
  LOG(Debug, "the [[ ]] conditional yielded status %lld",
      static_cast<long long>(status));
  cxt.set_last_exit_status(static_cast<i32>(status));
  return status;
}

ArithmeticCommand::ArithmeticCommand(SourceLocation location, String expression)
    : CompoundCommand(location), m_expression(steal(expression))
{}

ArithmeticCommand::~ArithmeticCommand() = default;

cold fn ArithmeticCommand::to_string() const throws -> String
{
  return "ArithmeticCommand";
}

cold fn ArithmeticCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + " \"" +
         m_expression.view() + "\"]";
}

/* A clause that holds only spaces or tabs is treated as omitted, the way bash
   reads for (( ; ; )) as an empty header rather than three blank expressions.
 */
static pure fn is_blank_clause(StringView text) wontthrow -> bool
{
  for (usize i = 0; i < text.length; i++)
    if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n') return false;
  return true;
}

fn ArithmeticCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  LOG(Debug, "evaluating the arithmetic command '%s'", m_expression.c_str());

  cxt.set_current_location(source_location());

  /* An empty (( )) is a failure with no evaluation, the way bash reads it as a
     null expression that yields status 1 rather than a parse error. */
  if (is_blank_clause(m_expression.view())) {
    cxt.set_last_exit_status(1);
    return 1;
  }

  /* A non-zero arithmetic value is success, a zero value is failure, the
     opposite of the value-to-status convention of the rest of the shell. The
     arithmetic evaluator reports a malformed expression or a division by zero
     as a plain error, so it is relocated to this command's position to carry a
     caret at the (( in the source, including the source an eval runs. */
  i64 value;
  try {
    value = cxt.evaluate_arithmetic(m_expression.view());
  } catch (const Error &e) {
    throw relocate_error(e, source_location());
  }
  const i64 status = value != 0 ? 0 : 1;
  cxt.set_last_exit_status(static_cast<i32>(status));
  return status;
}

cold fn ArithmeticCommand::analyze(AnalysisContext &actx,
                                   bool is_unconditional) const throws -> void
{
  unused(is_unconditional);
  /* The expression may assign any name, as in (( i = 10 )), and the prepass
     does not parse it, so every recorded constant is forgotten rather than risk
     folding a later read to a value this command overwrote. */
  actx.constant_variables.clear();
}

cold fn SelectLoop::analyze(AnalysisContext &actx,
                            bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);
  unused(is_unconditional);
  /* The body runs repeatedly and may reassign a name, so a value recorded
     before the loop does not hold inside it. */
  actx.constant_variables.clear();
  m_body->analyze(actx, false);
}

cold fn SelectLoop::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_body != nullptr);
  m_body->register_defined_functions(actx);
}

CStyleForLoop::CStyleForLoop(SourceLocation location, String init,
                             String condition, String step,
                             const Expression *body)
    : CompoundCommand(location), m_init(steal(init)),
      m_condition(steal(condition)), m_step(steal(step)), m_body(body)
{}

CStyleForLoop::~CStyleForLoop() = default;

cold fn CStyleForLoop::to_string() const throws -> String
{
  return "CStyleForLoop";
}

cold fn CStyleForLoop::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);
  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + " \"" + m_init.view() + ";" +
         m_condition.view() + ";" + m_step.view() + "\"]\n" + pad +
         EXPRESSION_AST_INDENT + m_body->to_ast_string(layer + 1);
}

ArrayAssignCommand::ArrayAssignCommand(SourceLocation location, StringView name,
                                       ArrayList<const Token *> elements,
                                       bool is_append)
    : Command(location), m_name(name), m_elements(steal(elements)),
      m_is_append(is_append)
{}

ArrayAssignCommand::~ArrayAssignCommand() = default;

cold fn ArrayAssignCommand::to_string() const throws -> String
{
  return "ArrayAssignCommand";
}

cold fn ArrayAssignCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + " " + m_name.view() +
         (m_is_append ? "+=(...)" : "=(...)") + "]";
}

fn ArrayAssignCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  cxt.set_current_location(source_location());

  /* The elements expand the way command arguments do, with field splitting and
     globbing, so a=( $list *.txt ) builds the array bash would. */
  ArrayList<String> values = cxt.process_args(m_elements, false, true);
  LOG(Debug, "assigning %zu elements to the array '%s'", values.count(),
      m_name.c_str());
  cxt.assign_indexed_array_elements(m_name.view(), steal(values), m_is_append);
  cxt.set_last_exit_status(0);
  return 0;
}

cold fn ArrayAssignCommand::analyze(AnalysisContext &actx,
                                    bool is_unconditional) const throws -> void
{
  unused(is_unconditional);
  /* An array assignment makes the name no longer a scalar literal, so the
     constant table forgets it rather than letting a prior scalar constant fold
     a later $name or $((name)) to a stale value. */
  actx.constant_variables.erase(m_name.view());
}

fn CStyleForLoop::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);

  cxt.set_terminal_exec_allowed(false);

  /* The analyze pass proved the body never runs, the condition folds to a
     constant zero, so the loop yields 0 without evaluating the init or the
     condition. */
  if (m_is_fully_eliminated) {
    LOG(Debug, "running the fully eliminated c-style for as a no-op");
    cxt.set_last_exit_status(0);
    return 0;
  }

  cxt.set_current_location(source_location());

  LOG(Debug,
      "entering the c-style for loop with init '%s', condition '%s', step "
      "'%s'",
      m_init.c_str(), m_condition.c_str(), m_step.c_str());

  if (!is_blank_clause(m_init.view())) cxt.evaluate_arithmetic(m_init.view());

  cxt.enter_loop();
  defer { cxt.leave_loop(); };

  i64 ret = 0;
  /* An empty condition is always true, the way for ((;;)) loops forever. A
     condition the folding rule proved constant reads its cached value rather
     than re-parsing the clause on every pass. */
  while (is_blank_clause(m_condition.view()) ||
         (m_folded_condition.has_value()
              ? (*m_folded_condition != 0)
              : cxt.evaluate_arithmetic_cached_clause(
                    m_condition.view(), m_condition_tokens,
                    m_condition_tokenized, m_condition_simple) != 0))
  {
    ret = m_body->evaluate(cxt);
    if (resolve_loop_control(cxt) == loop_disposition::StopLoop) break;
    /* The step runs after the body on every iteration, including one ended by a
       continue, the way bash advances the counter. */
    if (!is_blank_clause(m_step.view())) {
      cxt.evaluate_arithmetic_cached_clause(m_step.view(), m_step_tokens,
                                            m_step_tokenized, m_step_simple);
    }
  }
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn CStyleForLoop::analyze(AnalysisContext &actx,
                               bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);
  unused(is_unconditional);

  /* The C-style-for folding rule reads the three clauses while they are still
     unchanged, so the optimizer runs before the constant table is cleared for
     the body. */
  optimizer::optimize_node(this, actx);

  /* The header and body reassign the counter on every iteration, so a constant
     recorded before the loop does not hold inside or after it. */
  actx.constant_variables.clear();
  m_body->analyze(actx, false);
}

cold fn CStyleForLoop::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_body != nullptr);
  m_body->register_defined_functions(actx);
}

pure fn CStyleForLoop::condition_clause() const wontthrow -> StringView
{
  return m_condition.view();
}

pure fn CStyleForLoop::init_clause() const wontthrow -> StringView
{
  return m_init.view();
}

fn CStyleForLoop::set_folded_condition(i64 value) const wontthrow -> void
{
  m_folded_condition = value;
}

pure fn CStyleForLoop::has_folded_condition() const wontthrow -> bool
{
  return m_folded_condition.has_value();
}

fn CStyleForLoop::as_cstyle_for_loop() const wontthrow -> const CStyleForLoop *
{
  return this;
}

Subshell::Subshell(SourceLocation location, const Expression *body)
    : CompoundCommand(location), m_body(body)
{}

Subshell::~Subshell() = default;

cold fn Subshell::to_string() const throws -> String { return "Subshell"; }

cold fn Subshell::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);

  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

fn Subshell::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);

  /* A subshell isolates its commands, so none of them may replace the shell
     process. The in_subshell guard already blocks the terminal exec, and the
     flag is cleared here as well so the intent reads at the boundary. */
  cxt.set_terminal_exec_allowed(false);

  /* This shell has no process-level subshell, so isolate by snapshot. A cd or
     an assignment inside does not leak, but the exit status propagates. An exit
     inside ends only the subshell. */
  /* A loop in the parent is not the subshell's to break, so the body runs with
     a fresh loop count and the parent's count returns afterward. */
  let const saved_loop_depth = cxt.loop_depth();
  cxt.set_loop_depth(0);
  defer { cxt.set_loop_depth(saved_loop_depth); };

  LOG(Debug, "entering the snapshot subshell");

  let snapshot = cxt.snapshot_state();

  /* The subshell body's transient scratch is reclaimed at the boundary, so a
     ( ... ) inside a loop does not grow the arena across iterations. The status
     is an integer and restore_state reverts the inner state, so nothing the
     release frees is still read. The defer runs after restore_state on both the
     normal and the thrown path. */
  let const subshell_mark = cxt.scratch_mark();
  defer { cxt.scratch_release(subshell_mark); };
  cxt.enter_subshell();
  /* The inherited EXIT action belongs to the parent, so it does not fire at the
     subshell's end. An EXIT action the body sets survives this clear and fires
     below. */
  cxt.clear_inherited_exit_trap();
  i64 ret = 0;
  /* A diagnostic thrown by the body, such as a readonly violation or a missing
     command, must still restore the snapshot and leave the subshell, otherwise
     the parent stays stuck in subshell mode with the inner state leaked. In
     bash mood a script-fatal error, the set -u read and the ${name:?} report,
     ends only the subshell the way bash confines the abort to the child
     process, reported here as status 1 the way bash answers it. */
  try {
    ret = m_body->evaluate(cxt);
  } catch (const ErrorBase &error) {
    /* A forked subshell would confine the abort to the child, so the snapshot
       subshell confines a script-fatal error the same way in every mood,
       status 1 the way bash answers it and 2 the way dash does, which the
       default mood follows. */
    if (error.is_script_fatal()) {
      LOG(Debug, "the subshell confined a script-fatal error: %s",
          error.message().c_str());
      const String *source = cxt.current_source();
      show_message(
          error.to_string(source != nullptr ? source->view() : StringView{}));
      ret = cxt.is_bash_compatible() ? 1 : 2;
      cxt.set_last_exit_status(static_cast<i32>(ret));
      cxt.clear_control_flow();
    } else {
      cxt.run_subshell_exit_trap();
      cxt.leave_subshell();
      cxt.restore_state(steal(snapshot));
      throw;
    }
  } catch (...) {
    cxt.run_subshell_exit_trap();
    cxt.leave_subshell();
    cxt.restore_state(steal(snapshot));
    throw;
  }

  /* An exit inside the subshell ends only the subshell and supplies its status.
     A break or a continue is scoped to a loop inside the subshell, so it does
     not escape into the parent's loop and is consumed here. A return stays
     pending and propagates after the state is restored. */
  if (cxt.has_pending_control_flow()) {
    const control_flow::Kind kind = cxt.pending_control_flow().kind;
    if (kind == control_flow::Kind::Exit) {
      ret = cxt.pending_control_flow().value;
      cxt.clear_control_flow();
    } else if (kind == control_flow::Kind::Break ||
               kind == control_flow::Kind::Continue)
    {
      cxt.clear_control_flow();
    }
  }

  /* The subshell ends here, so its own EXIT action runs now, in the subshell's
     state and before the parent's traps return. */
  cxt.run_subshell_exit_trap();
  cxt.leave_subshell();
  cxt.restore_state(steal(snapshot));
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn Subshell::analyze(AnalysisContext &actx,
                          bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  /* A subshell runs in a forked child, so an assignment in its body never
     changes a parent variable. The outer constants are saved and restored
     around the body, and the body itself starts from an empty table so it does
     not carry a propagation across the subshell boundary. */
  let saved_constants = actx.constant_variables.clone();
  actx.constant_variables.clear();
  m_body->analyze(actx, is_unconditional);
  actx.constant_variables = steal(saved_constants);
}

FunctionDefinition::FunctionDefinition(SourceLocation location, StringView name,
                                       const Expression *body)
    : CompoundCommand(location), m_name(name), m_body(body)
{}

/* The body is owned by the function table rather than this node, so it is not
   deleted here. */
FunctionDefinition::~FunctionDefinition() = default;

pure fn FunctionDefinition::name() const wontthrow -> const String &
{
  return m_name;
}

pure fn FunctionDefinition::body() const wontthrow -> const Expression *
{
  return m_body;
}

cold fn FunctionDefinition::to_string() const throws -> String
{
  let result = String{"FunctionDefinition \""};
  result += StringView{m_name};
  result += "\"";
  return result;
}

cold fn FunctionDefinition::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);

  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

fn FunctionDefinition::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);

  /* The recorded definition is a "name () " line then the body's source span,
     the shape bash prints from declare -f. ble.sh clones a function by
     replacing the leading name in this text and greps the "name ()" line, so
     both matter. The source string dies with its frame while the function lives
     on, so the store keeps its own copy. An unrecorded body span stores empty
     text. */
  let definition_text = String{cxt.scratch_allocator()};
  if (const String *source = cxt.current_source();
      source != nullptr &&
      m_body->source_end_position() > m_body->source_location().position &&
      m_body->source_end_position() <= source->count())
  {
    definition_text.append(m_name.view());
    definition_text.append(StringView{" () \n"});
    definition_text.append(source->view().substring_of_length(
        m_body->source_location().position,
        m_body->source_end_position() - m_body->source_location().position));
  }
  LOG(Info, "registering the function '%s'%s", m_name.c_str(),
      definition_text.is_empty() ? " without recorded definition text" : "");
  cxt.register_function(m_name, m_body, definition_text.view(),
                        m_body->source_location().position, source_location());
  cxt.set_last_exit_status(0);
  return 0;
}

cold fn FunctionDefinition::analyze(AnalysisContext &actx,
                                    bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  unused(is_unconditional);
  actx.defined_functions.add(m_name);

  /* The body runs later when the function is called, not where it is defined,
     so a constant recorded before the definition does not prove a value inside
     the body, and an assignment in the body does not reach the straight-line
     block around the definition. The outer constants are saved and restored
     around the body, and the body starts from an empty table. */
  let saved_constants = actx.constant_variables.clone();
  actx.constant_variables.clear();
  let saved_locals = steal(actx.function_local_names);
  actx.function_local_names = HashSet{heap_allocator()};
  actx.function_scope_depth++;
  m_body->analyze(actx, false);
  actx.function_scope_depth--;
  actx.function_local_names = steal(saved_locals);
  actx.constant_variables = steal(saved_constants);
}

cold fn FunctionDefinition::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  actx.defined_functions.add(m_name);
}

RedirectedCommand::RedirectedCommand(SourceLocation location,
                                     const Command *child,
                                     ArrayList<Redirection> &&redirections)
    : Command(location), m_child(child)
{
  m_redirections = steal(redirections);
}

RedirectedCommand::~RedirectedCommand() = default;

cold fn RedirectedCommand::to_string() const throws -> String
{
  return "RedirectedCommand";
}

cold fn RedirectedCommand::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_child != nullptr);

  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_child->to_ast_string(layer + 1);
}

cold fn RedirectedCommand::analyze(AnalysisContext &actx,
                                   bool is_unconditional) const throws -> void
{
  ASSERT(m_child != nullptr);

  m_child->analyze(actx, is_unconditional);
}

cold fn RedirectedCommand::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_child != nullptr);

  m_child->register_defined_functions(actx);
}

fn RedirectedCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_child != nullptr);

  LOG(Debug, "applying %zu redirections around the compound command",
      m_redirections.count());

  cxt.set_current_location(source_location());

  /* A <(...) or >(...) in a redirection target, as in done < <(cmd), opens a
     pipe and forks a child or leaves a temp file during the expansion below.
     The mark is taken before it so this command reaps only the substitution its
     own redirection opens, reaped and its temp file deleted once the redirected
     command returns. Registered first so it runs last, after the descriptor
     backups restore. */
  let const substitution_mark = cxt.mark_process_substitutions();
  defer { cxt.cleanup_process_substitutions(substitution_mark); };

  /* The child runs around saved descriptor backups that restore afterward, so
     it forks rather than replacing the shell. */
  cxt.set_terminal_exec_allowed(false);

  /* The child runs in the shell process, so each redirection points one of the
     shell's own descriptors at the target and the saved backups put them back.
     The backups restore in reverse on every exit path, a normal return, a
     thrown diagnostic, or a pending break, continue, return, or exit that
     propagates through the child. */
  ArrayList<os::saved_descriptor> saved_descriptors{cxt.scratch_allocator()};
  defer
  {
    shit::flush();
    for (usize i = saved_descriptors.count(); i > 0; i--)
      os::restore_descriptor(saved_descriptors[i - 1]);
  };

  shit::flush();

  for (let const &redir : m_redirections) {
    let r = resolve_redirection(redir, cxt, source_location());
    r.target_fd =
        allocate_redirection_descriptor(redir, r, cxt, source_location());
    switch (r.kind) {
    /* A staged heredoc body and an opened file both dup onto the target shell
       descriptor for the compound's duration, the opened copy closing at once
       since the dup keeps a live one. */
    case redirection_outcome::Heredoc:
    case redirection_outcome::OpenedFile:
      saved_descriptors.push(
          os::save_and_replace_descriptor(r.target_fd, r.opened_fd));
      os::close_fd(r.opened_fd);
      break;
    /* The both-streams file points fd 1 at it and fd 2 follows for the
       compound's duration. */
    case redirection_outcome::BothStreams: {
      const os::saved_descriptor saved_out =
          os::save_and_replace_descriptor(1, r.opened_fd);
      saved_descriptors.push(saved_out);
      const os::saved_descriptor saved_err =
          os::save_and_replace_descriptor(2, r.opened_fd);
      saved_descriptors.push(saved_err);
      os::close_fd(r.opened_fd);
      if (!saved_out.is_dup2_ok || !saved_err.is_dup2_ok) {
        throw ErrorWithLocation{redir.target->source_location(),
                                "Bad file descriptor"};
      }
      break;
    }
    case redirection_outcome::Duplicate: {
      /* The close form backs the descriptor up, then closes it. The backup is
         saved so restore reopens it when the compound command finishes. */
      if (r.dup_from_fd == Redirection::DUP_FD_CLOSE) {
        saved_descriptors.push(os::save_and_replace_descriptor(
            r.target_fd, os::descriptor_for_shell_fd(r.target_fd)));
        os::close_fd(os::descriptor_for_shell_fd(r.target_fd));
        break;
      }

      const os::descriptor source = os::descriptor_for_shell_fd(r.dup_from_fd);
      const os::saved_descriptor saved =
          os::save_and_replace_descriptor(r.target_fd, source);
      saved_descriptors.push(saved);
      /* A duplication onto a closed or invalid descriptor, as in { echo hi; }
         >&7 with fd 7 closed, fails the dup2. The compound command fails with a
         located error rather than writing to the original descriptor, matching
         dash. */
      if (!saved.is_dup2_ok) {
        const SourceLocation location = redir.target != nullptr
                                            ? redir.target->source_location()
                                            : source_location();
        throw ErrorWithLocation{location, String::from(r.dup_from_fd, heap_allocator()) +
                                              ": Bad file descriptor"};
      }
      break;
    }
    }
  }

  const i64 result = m_child->evaluate(cxt);
  return result;
}

UnaryExpression::UnaryExpression(SourceLocation location, const Expression *rhs)
    : Expression(location), m_rhs(rhs)
{}

UnaryExpression::~UnaryExpression() = default;

cold fn UnaryExpression::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_rhs != nullptr);

  let s = String{heap_allocator()};
  let const pad = indent_for_layer(layer);

  s += pad + "[Unary " + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_rhs->to_ast_string(layer + 1);
  return s;
}

BinaryExpression::BinaryExpression(SourceLocation location,
                                   const Expression *lhs, const Expression *rhs)
    : Expression(location), m_lhs(lhs), m_rhs(rhs)
{}

BinaryExpression::~BinaryExpression() = default;

cold fn BinaryExpression::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_lhs != nullptr);
  ASSERT(m_rhs != nullptr);

  let s = String{heap_allocator()};
  let const pad = indent_for_layer(layer);

  s += pad + "[Binary " + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_lhs->to_ast_string(layer + 1) + "\n";
  s += pad + EXPRESSION_AST_INDENT + m_rhs->to_ast_string(layer + 1);

  return s;
}

ConstantNumber::ConstantNumber(SourceLocation location, i64 value)
    : Expression(location), m_value(value)
{}

ConstantNumber::~ConstantNumber() = default;

fn ConstantNumber::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  unused(cxt);
  return m_value;
}

cold fn ConstantNumber::to_ast_string(usize layer) const throws -> String
{
  let s = String{heap_allocator()};
  let const pad = indent_for_layer(layer);

  s += pad + "[Number " + to_string() + "]";
  return s;
}

cold fn ConstantNumber::to_string() const throws -> String
{
  return String::from(m_value, heap_allocator());
}

ConstantString::ConstantString(SourceLocation location, StringView value)
    : Expression(location), m_value(value)
{}

ConstantString::~ConstantString() = default;

fn ConstantString::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  unused(cxt);
  unreachable();
}

cold fn ConstantString::to_ast_string(usize layer) const throws -> String
{
  let s = String{heap_allocator()};
  let const pad = indent_for_layer(layer);

  s += pad + "[String \"" + to_string() + "\"]";
  return s;
}

cold fn ConstantString::to_string() const throws -> String { return m_value; }

#define UNARY_EXPRESSION_DECLS(e, expr)                                        \
  e::e(SourceLocation location, const Expression *rhs)                         \
      : UnaryExpression(location, rhs)                                         \
  {}                                                                           \
  String e::to_string() const throws { return #expr; }                         \
  i64 e::evaluate_impl(EvalContext &cxt) const throws                          \
  {                                                                            \
    return expr m_rhs->evaluate(cxt);                                          \
  }

UNARY_EXPRESSION_DECLS(Negate, -);
UNARY_EXPRESSION_DECLS(Unnegate, +);
UNARY_EXPRESSION_DECLS(LogicalNot, !);
UNARY_EXPRESSION_DECLS(BinaryComplement, ~);

BinaryDummyExpression::BinaryDummyExpression(SourceLocation location,
                                             const Expression *lhs,
                                             const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

cold fn BinaryDummyExpression::to_string() const throws -> String
{
  return "BinaryDummyExpression";
}

fn BinaryDummyExpression::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  unused(cxt);
  return 0;
}

Divide::Divide(SourceLocation location, const Expression *lhs,
               const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

cold fn Divide::to_string() const throws -> String { return "/"; }

fn Divide::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_lhs != nullptr);
  ASSERT(m_rhs != nullptr);

  let const denom = m_rhs->evaluate(cxt);
  if (denom == 0)
    throw ErrorWithLocation{m_rhs->source_location(), "Division by zero"};

  return m_lhs->evaluate(cxt) / denom;
}

#define BINARY_EXPRESSION_DECLS(e, expr)                                       \
  e::e(SourceLocation location, const Expression *lhs, const Expression *rhs)  \
      : BinaryExpression(location, lhs, rhs)                                   \
  {}                                                                           \
  String e::to_string() const throws { return #expr; }                         \
  i64 e::evaluate_impl(EvalContext &cxt) const throws                          \
  {                                                                            \
    return m_lhs->evaluate(cxt) expr m_rhs->evaluate(cxt);                     \
  }

BINARY_EXPRESSION_DECLS(Add, +);
BINARY_EXPRESSION_DECLS(Subtract, -);
BINARY_EXPRESSION_DECLS(Multiply, *);
BINARY_EXPRESSION_DECLS(Module, %);
BINARY_EXPRESSION_DECLS(BinaryAnd, &);
BINARY_EXPRESSION_DECLS(LogicalAnd, &&);
BINARY_EXPRESSION_DECLS(GreaterThan, >);
BINARY_EXPRESSION_DECLS(GreaterOrEqual, >=);
BINARY_EXPRESSION_DECLS(RightShift, >>);
BINARY_EXPRESSION_DECLS(LessThan, <);
BINARY_EXPRESSION_DECLS(LessOrEqual, <=);
BINARY_EXPRESSION_DECLS(LeftShift, <<);
BINARY_EXPRESSION_DECLS(BinaryOr, |);
BINARY_EXPRESSION_DECLS(LogicalOr, ||);
BINARY_EXPRESSION_DECLS(Xor, ^);
BINARY_EXPRESSION_DECLS(Equal, ==);
BINARY_EXPRESSION_DECLS(NotEqual, !=);

} // namespace expressions

} // namespace shit
