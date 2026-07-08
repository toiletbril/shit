#pragma once

#include "Common.hpp"
#include "StringView.hpp"

namespace shit {

namespace colors {

namespace ansi {
inline const StringView RESET = "\x1b[0m";
inline const StringView BOLD = "\x1b[1m";
inline const StringView DIM = "\x1b[2m";
inline const StringView INVERSE = "\x1b[7m";
inline const StringView RED = "\x1b[31m";
inline const StringView GREEN = "\x1b[32m";
inline const StringView YELLOW = "\x1b[33m";
inline const StringView BLUE = "\x1b[34m";
/* The high-intensity foreground colors, distinct from the bold attribute. */
inline const StringView BRIGHT_BLUE = "\x1b[94m";
inline const StringView BRIGHT_CYAN = "\x1b[96m";
inline const StringView GRAY = "\x1b[38;5;250m";
inline const StringView BOLD_RED = "\x1b[1;31m";
inline const StringView BOLD_GREEN = "\x1b[1;32m";
inline const StringView BOLD_YELLOW = "\x1b[1;33m";
inline const StringView CYAN = "\x1b[36m";
inline const StringView BOLD_MAGENTA = "\x1b[1;35m";
inline const StringView BOLD_CYAN = "\x1b[1;36m";
inline const StringView BOLD_WHITE = "\x1b[1;37m";
} /* namespace ansi */

/* Whether color may be written to a stream, decided fresh so a redirected
   stream never gains escapes. Color is on only when the stream is a terminal,
   NO_COLOR is unset or empty, and TERM is not dumb. */
fn stdout_wants_color() throws -> bool;
fn stderr_wants_color() throws -> bool;

} /* namespace colors */

} /* namespace shit */
