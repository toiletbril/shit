#!/bin/sh
# type, command, alias, eval, and the no-op builtins, checked against dash.

# The shell builtins report themselves consistently.
type echo
type test
command -v set

# eval joins and runs its arguments.
eval 'evaluated=yes'
echo "eval=$evaluated"
cmd='echo from_eval'
eval "$cmd"

# true, false, and the colon set the status without output.
true; echo "true=$?"
false; echo "false=$?"
:; echo "colon=$?"

# An alias defined and used on later lines expands.
alias hi='echo aliased'
hi
unalias hi
