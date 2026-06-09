#!/bin/bash
# bash accepts == as a synonym for = in test and [, so the bash mood does too.
[ foo == foo ] && echo bracket-eq
[ foo == bar ] || echo bracket-neq
test foo == foo && echo test-eq
test "$HOME" == "$HOME" && echo var-eq
[ abc != xyz ] && echo neq-still-works
