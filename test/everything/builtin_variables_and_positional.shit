#!/bin/sh
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
