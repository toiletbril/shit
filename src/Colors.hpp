#pragma once

#include "Common.hpp"
#include "StringView.hpp"

namespace shit {

namespace colors {

/* ANSI SGR sequences shared across the shell, the diagnostics, the job list,
   and the benchmark output. A reset clears every attribute a colored span set.
   The bold palette follows clang's diagnostic colors so the shell looks
   consistent.
 */
namespace ansi {
inline const StringView RESET = "\x1b[0m";
inline const StringView BOLD = "\x1b[1m";
inline const StringView DIM = "\x1b[2m";
inline const StringView RED = "\x1b[31m";
inline const StringView GREEN = "\x1b[32m";
inline const StringView YELLOW = "\x1b[33m";
inline const StringView BOLD_RED = "\x1b[1;31m";
inline const StringView BOLD_GREEN = "\x1b[1;32m";
inline const StringView BOLD_YELLOW = "\x1b[1;33m";
inline const StringView BOLD_MAGENTA = "\x1b[1;35m";
inline const StringView BOLD_CYAN = "\x1b[1;36m";
} /* namespace ansi */

/* Whether color may be written to a stream, decided fresh so a redirected
   stream never gains escapes. Color is on only when the stream is a terminal,
   NO_COLOR is unset or empty, and TERM is not dumb. */
fn stdout_wants_color() throws -> bool;
fn stderr_wants_color() throws -> bool;

} /* namespace colors */

} /* namespace shit */
