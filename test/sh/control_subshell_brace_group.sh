#!/bin/sh
# Subshell isolation versus brace-group shared state, and command/eval/: builtins.

x=outer
( x=inner; echo "in subshell $x" )
echo "after subshell $x"

y=before
{ y=after; echo "in brace $y"; }
echo "after brace $y"

# redirection applied to a compound statement.
{
    echo line one
    echo line two
} | cat

for n in 1 2 3; do
    echo "redir $n"
done > /tmp/control_subshell_brace_group.$$
cat /tmp/control_subshell_brace_group.$$
rm -f /tmp/control_subshell_brace_group.$$

# the : null builtin always succeeds.
:
echo "colon rc $?"
: ignored arguments
echo "colon args rc $?"

# eval builds and runs a command.
cmd="echo evaluated text"
eval "$cmd"

# command runs a builtin directly.
command echo "command builtin"

# subshell exit status propagates.
( exit 5 )
echo "subshell exit rc $?"
