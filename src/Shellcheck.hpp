#pragma once

#include "StringView.hpp"

namespace shit {

/* One shellcheck-style static check the analysis stage reports, paired with the
   shellcheck code it mirrors so --list and the source that emits the warning
   agree on the name. */
struct shellcheck_check
{
  StringView code;
  StringView summary;
};

inline const shellcheck_check SHELLCHECK_CHECKS[] = {
    {"SC2162", "read without -r mangles a backslash in the input"          },
    {"SC2059", "printf format from a variable can inject format directives"},
};

} /* namespace shit */
