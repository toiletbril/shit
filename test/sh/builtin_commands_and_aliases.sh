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


# export -p and readonly list in the POSIX form dash reloads. The listing is
# filtered to the test's own names so the inherited environment cannot perturb
# the comparison.
export EV=one
echo "export_p=$(export -p | grep "^export EV=")"
readonly RV=two
echo "readonly_list=$(readonly | grep "RV=")"

# Function-local variables restore the outer value on return.

outer=global
show() {
    local outer=inner
    echo "inside=$outer"
}
show
echo "outside=$outer"

# A bare local name shadows with an empty value.
probe() {
    local empty
    echo "empty=[$empty]"
}
probe

# Positional parameters are saved and restored across a function call.
set -- a b c
withargs() {
    echo "fn_args=$# $1"
}
withargs x y
echo "outer_args=$# $1"

# read rejects the bash-only options under dash, so a read -n fails, and let is
# not a builtin under dash, so it is reported not found.
echo x | read -n 1 y; echo "read_n=$?"
let x=1 2>/dev/null; echo "let=$?"

# Variable builtins and positional parameters, checked against dash.

# export then unset must leave the variable fully unset, including the
# environment copy that export creates.
export EXPORTED=value
echo "exported=$EXPORTED"
unset EXPORTED
echo "after_unset=[$EXPORTED]"

# A plain assignment, read, and unset round-trip.
plain=hello
echo "plain=$plain"
unset plain
echo "after_unset_plain=[$plain]"

# readonly rejects reassignment but the first value stands.
readonly RO=locked
echo "ro=$RO"

# Positional parameters through set and shift.
set -- one two three four
echo "count=$# first=$1 third=$3"
echo "all=$@"
shift
echo "after_shift count=$# first=$1"
shift 2
echo "after_shift2 count=$# first=$1"

# set with no operands does not clobber the positionals here.
echo "still=$1"

# Special builtins keep their prefix assignments after the command, and a
# redirection error fails a regular command but does not abort the shell,
# checked against dash. A non-special command's prefix assignment stays
# temporary.

# A prefix assignment before a special builtin persists, unexported.
x=2 eval ":"
echo "persist=[$x]"
sh -c 'echo "child=[$x]"'

# A prefix before a regular builtin does not persist.
y=9 true
echo "temporary=[$y]"

# export and readonly are special, so their prefixes persist too.
foo=bar export baz=1
echo "export_prefix=[$foo][$baz]"

# A redirection that cannot open its target fails the command but the shell
# continues to the next one.
true > /no/such/directory/file 2>/dev/null
echo "after_bad_redirect=$?"
echo still_running
