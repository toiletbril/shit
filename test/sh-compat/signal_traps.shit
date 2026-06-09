#!/bin/sh
# Signal traps run their action when the signal arrives, ignore the signal with
# an empty action, and reset to the default on removal, checked against dash. The
# shell signals its own pid so the test needs no second process.

trap 'echo got_usr1' USR1
kill -USR1 $$
echo after_usr1

cleanup_ran=0
trap 'echo got_term; cleanup_ran=1' TERM
kill -TERM $$
echo "term_handled=$cleanup_ran"

trap '' USR2
kill -USR2 $$
echo ignored_usr2

# The action runs once per arrival inside a loop, before the next command.
trap 'echo loop_hit' USR1
i=0
while [ $i -lt 3 ]; do
    kill -USR1 $$
    i=$((i + 1))
done
echo loop_done

# A trap removed returns to the default. Sending the signal afterwards would
# terminate the shell, so only the table change is checked here.
trap - USR1
trap - TERM USR2
echo traps_reset
