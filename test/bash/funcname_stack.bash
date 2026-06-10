#!/bin/bash
# FUNCNAME reads the call stack, the scalar the innermost frame, the array the
# whole stack, and the name is unset outside a function.
f() { echo "in=$FUNCNAME"; g; }
g() { echo "g0=${FUNCNAME[0]} g1=${FUNCNAME[1]} depth=${#FUNCNAME[@]}"; }
f
echo "outside=[${FUNCNAME-unset}]"
h() { for fr in "${FUNCNAME[@]}"; do echo "frame=$fr"; done; }
h
k() { builtin eval -- "function $FUNCNAME/sub { echo subbed; }"; "$FUNCNAME/sub"; }
k
