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
    {"SC2166", "test with -a or -o is obsolescent, join with && or ||"     },
    {"SC2236", "a negated -z is just -n"                                   },
    {"SC2237", "a negated -n is just -z"                                   },
    {"SC2046", "an unquoted command substitution can word-split, quote it" },
    {"SC2230", "which is non-standard, command -v is portable"             },
    {"SC2002", "a useless cat, pass the file to the next command"          },
    {"SC2244", "a one-operand test reads clearer with -n"                  },
    {"SC2335", "a negated numeric comparison has a direct operator"        },
    {"SC2249", "a case with no default branch can miss an input"           },
    {"SC3014", "== is undefined in POSIX test, use ="                      },
};

} /* namespace shit */
