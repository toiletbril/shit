#pragma once

/* The analyze-time optimizer. The prepass calls into this unit to prove a fact
   about a node once, before any command runs, so the evaluator skips work on
   every later execution of that node. The routines here decide a constant
   arithmetic value, a static loop or if verdict, and the plain-literal class of
   a word. Each routine reads the AST and the analysis context and returns the
   decision. The node stores the decision in its own cache field and the
   evaluator reads it, so this unit holds the algorithms and the nodes hold the
   storage and the dispatch. */

#include "Common.hpp"
#include "Containers.hpp"
#include "Maybe.hpp"
#include "Tokens.hpp"

namespace shit {

class Token;
class AnalysisContext;

namespace optimizer {

/* Evaluate an arithmetic expression that holds only literals and operators,
   with no variable and no substitution, so the result is a compile-time
   constant. The analyze pass calls this to fold a constant $((...)) once
   instead of leaving the evaluator to re-parse it on every expansion. Returns
   None when the expression is not provably constant or fails to evaluate. */
fn try_fold_constant_arithmetic(StringView expression) wontthrow -> Maybe<i64>;

/* The statically-decidable success of a simple command used as a loop or if
   condition. The caller passes the command words and the analysis context after
   the node has cleared its own guards, a redirection, an async or negated
   prefix, and a prefix assignment. Some(true) means the command always succeeds
   with status 0 and no side effect, Some(false) means it always fails, and None
   means the result is only known at run time. The verdict comes from a constant
   builtin such as true, false, or a literal test. */
fn simple_command_static_verdict(const ArrayList<const Token *> &args,
                                 const AnalysisContext &actx) throws
    -> Maybe<bool>;

/* True when the segment text holds an unquoted glob metacharacter, one of '*',
   '?', or '['. The plain-literal fast path in the evaluator consults this to
   decide whether a word may skip pathname expansion. */
pure fn word_segment_has_glob_metacharacter(const WordSegment &segment)
    wontthrow -> bool;

/* How a word may take the evaluator's plain-literal fast path. The fast path in
   process_args reads this to push a word that needs no expansion, splitting, or
   globbing straight to the argument vector. */
pure fn classify_plain_literal(const Word &word) wontthrow
    -> Word::PlainLiteral;

} /* namespace optimizer */

} /* namespace shit */
