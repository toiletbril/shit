#pragma once

/* Declarations shared across the Expressions translation units. The node method
   definitions split into ExpressionsSimpleCommand.cpp, ExpressionsCompound.cpp,
   and ExpressionsArith.cpp, while the free helpers and the redirection and loop
   types they share live here. The class definitions stay in Expressions.hpp. */

#include "Common.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Platform.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

fn indent_for_layer(usize layer) throws -> String;
fn report_command_not_found(EvalContext &cxt, const CommandNotFound &e) throws
    -> void;

/* Rebases a function-body error onto the stored definition copy when the error
   sits inside a live function call, so an error from an eval-defined or a
   sourced-file function renders against the body and the defining filename
   rather than the caller's line. The returned view is the windowed source, or
   None when no window applies and the caller renders against the current
   source. */
fn window_function_body_error(EvalContext &cxt,
                              ErrorWithLocation &error) wontthrow
    -> Maybe<StringView>;

namespace expressions {

/* What a single redirection resolves to before any descriptor is placed. */
enum class redirection_outcome
{
  Heredoc,     /* opened_fd holds a staged temp body for target_fd */
  OpenedFile,  /* opened_fd holds a freshly opened file for target_fd */
  BothStreams, /* opened_fd opens like >file, fd 1 and fd 2 both follow it */
  Duplicate,   /* dup_from_fd names the source, or DUP_FD_CLOSE for the close */
};

/* The unplaced result of one redirection, the opened descriptor or the
   duplication source, with the target fd. opened_fd is owned by the caller,
   which places it and closes it. dup_from_fd is the duplication source, the
   close marker, or the target itself for a self copy. */
struct resolved_redirection
{
  redirection_outcome kind{};
  i32 target_fd{-1};
  os::descriptor opened_fd{};
  i32 dup_from_fd{-1};
  bool is_cached{false};
};

fn resolve_redirection(const Redirection &redir, EvalContext &cxt,
                       SourceLocation fallback_location,
                       bool *open_or_stage_failed = nullptr,
                       bool allow_fd_memoization = false) throws
    -> resolved_redirection;

fn allocate_redirection_descriptor(const Redirection &redir,
                                   const resolved_redirection &resolved,
                                   EvalContext &cxt, SourceLocation location,
                                   bool *open_or_stage_failed = nullptr) throws
    -> i32;

/* What a loop does with the control flow pending after its body ran. */
enum class loop_disposition
{
  /* No jump, or a continue aimed here, so run the next iteration. */
  RunNext,
  /* A break aimed here, or a jump aimed at an outer loop that is now left
     pending, so this loop stops. */
  StopLoop,
};

fn resolve_loop_control(EvalContext &cxt) throws -> loop_disposition;

} /* namespace expressions */

} /* namespace shit */
