#!/bin/sh
# A builtin that re-runs a command as a pipeline stage, checked against dash.
# The pipe ends must reach the real fd 0, 1, and 2 so the sub-command the
# builtin spawns inherits them.

# eval running an external command reads the pipe and writes the next stage.
printf 'axbxc\n' | eval sed 's/x/_/g'

# eval running cat consumes the producer rather than the terminal.
echo hi | eval cat
echo hi | eval 'cat'

# eval running a compound command reads every line of the pipe.
printf 'a\nb\n' | eval 'while read l; do echo "[$l]"; done'

# command running an external command reads the pipe too.
echo data | command cat

# eval feeding a counting stage sees the whole input.
seq 1 3 | eval wc -l

# A nested eval still threads the pipe to the innermost command.
echo nested | eval 'eval cat'

# eval as a middle stage wires the pipe through both sides.
seq 1 4 | eval cat | wc -l

# command runs the operand and its arguments from the pipe.
printf 'a b c\n' | command tr ' ' '\n' | wc -l
