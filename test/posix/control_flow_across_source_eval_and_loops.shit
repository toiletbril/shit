#!/bin/sh
# return, break, and continue crossing dot-source, eval, and loop boundaries,
# checked against dash. These are the regressions the source-buffer migration
# exposed, where a return must end a dot-source but propagate through eval.

# A return at the top of a dot-sourced file ends the source with its status, and
# execution continues after the dot command.
printf 'echo in_source\nreturn 3\necho never\n' > /tmp/shit_compat_src.sh
. /tmp/shit_compat_src.sh
echo "after_source=$?"

# Sourcing inside a function returns from the source, not the function, so the
# function keeps running after the dot command.
sources_a_file() {
  . /tmp/shit_compat_src.sh
  echo "still_in_function=$?"
}
sources_a_file
echo "after_function=$?"

# A continue inside a sourced file propagates into the enclosing loop, so the
# echo after the dot command is skipped on every iteration.
printf 'continue\n' > /tmp/shit_compat_cont.sh
for value in 1 2 3; do
  . /tmp/shit_compat_cont.sh
  echo "loop_skipped=$value"
done
echo "after_loop"

rm -f /tmp/shit_compat_src.sh /tmp/shit_compat_cont.sh

# A return inside eval propagates rather than ending the eval, so at the top of
# a non-interactive script it ends the shell with the given status. Nothing
# after this line runs in either shell.
eval "return 5"
echo "this line never runs"
