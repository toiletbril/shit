#!/bin/sh
# Run the completion engine through the shell's --complete debug flag over a
# fixed set of cases and print the result. The cases complete against the fixed
# data directory and a PATH that points nowhere, so the output stays the same on
# every machine. The first argument is the shell binary to run.
#
# A test diffs this output against expected/completion.out, which proves the
# completion engine without a terminal.

BIN="$1"

run() {
  echo "$1"
  PATH=/nonexistent_xyz "$BIN" --complete "$2" "$3" . 2>/dev/null
}

run "# command position completes builtin prefix ech" "ech" 3
run "# command position completes several builtins for tr" "tr" 2
run "# argument position completes a directory prefix with a trailing slash" \
    "cat data/ab" 10
run "# argument position lists the entries of a directory" \
    "cat data/abcd/" 14
run "# a glob star token resolves to its match" "echo data/*g" 12
run "# a glob question mark token resolves to its match" "echo data/a?cd" 14
run "# a glob bracket token resolves to its match" "echo data/abcd[a-z]*" 20
