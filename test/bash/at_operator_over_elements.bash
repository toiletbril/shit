#!/bin/bash
# The @op transform maps over each array element and positional parameter,
# checked byte-for-byte against bash. The per-element value transforms Q, U, L,
# u, and the a attribute map, the star form joins under the first IFS byte, and
# the at form keeps each element its own field.
a=(a "b c" d)
printf '<%s>' "${a[@]@Q}"; echo
echo "${a[*]@Q}"
upper=(x Y z)
echo "${upper[@]@U}"
echo "${upper[@]@L}"
echo "${upper[@]@u}"
echo "${a[@]@a}"
ra=(1 2); readonly ra
echo "${ra[@]@a}"
set -- one "two three" four
printf '<%s>' "${@@Q}"; echo
echo "${@@U}"
echo "${*@u}"
