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
    {"SC2115", "rm -r on \"$var/\" deletes / when the variable is empty"   },
    {"SC2114", "rm -r aimed at a system directory"                         },
    {"SC2062", "an unquoted grep pattern can glob against local files"     },
    {"SC2063", "a grep pattern that looks like a glob, grep reads regex"   },
    {"SC2069", "2>&1 before the file redirect sends stderr to the tty"     },
    {"SC2094", "reading and writing the same file in one command truncates"},
    {"SC2116", "a useless echo inside a command substitution"              },
    {"SC2242", "an exit or return code is not a number from 0 to 255"      },
    {"SC2145", "$@ inside a longer word concatenates unpredictably"        },
    {"SC2068", "an unquoted $@ word-splits and globs each argument"        },
    {"SC2174", "mkdir -pm applies the mode to the deepest directory only"  },
    {"SC2157", "a literal test operand makes the test constant"            },
    {"SC2170", "a numeric test operator on a non-numeric literal"          },
    {"SC2081", "[ cannot glob-match, use case or [[ ]]"                    },
    {"SC2143", "grep in a test substitution buffers it all, use grep -q"   },
    {"SC2013", "for over command output iterates words, use while read -r" },
    {"SC2044", "for over find output breaks on whitespace, use find -exec" },
    {"SC2038", "find piped to xargs breaks on special names, use -print0"  },
    {"SC2216", "piping into a command that ignores stdin"                  },
    {"SC2217", "redirecting input into a command that ignores stdin"       },
    {"SC2064", "a double-quoted trap action expands when set, not when fired"},
    {"SC3037", "echo flags are not in POSIX echo, use printf"               },
    {"SC3044", "declare is not in POSIX, assign plainly under a sh shebang"  },
    {"SC3046", "source is the bash spelling, the POSIX dot command is ."     },
    {"SC2196", "egrep is deprecated, use grep -E"                           },
    {"SC2197", "fgrep is deprecated, use grep -F"                           },
    {"SC2003", "expr forks for arithmetic the shell does natively"          },
    {"SC2219", "let runs arithmetic as a command, use ((...))"              },
    {"SC2168", "local outside a function has no scope to bind"              },
    {"SC2005", "echo of a command substitution is redundant"               },
    {"SC2009", "grepping ps output races the table, use pgrep"              },
    {"SC2010", "grepping ls output mangles names, use a glob or find"       },
    {"SC2126", "wc -l on grep output, use grep -c to count matches"         },
    {"SC2155", "declare and assign separately so the exit status is seen"   },
    {"SC2181", "test the command directly rather than $? indirectly"        },
    {"SC3043", "local is not in POSIX sh, the value stays global"           },
    {"SC3045", "printf -v is a bash extension absent from POSIX printf"     },
    {"SC3030", "mapfile and readarray are bash array builtins absent from sh"},
};

} /* namespace shit */
