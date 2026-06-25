#!/bin/bash
# The ${var@op} parameter transforms, checked byte-for-byte against bash. Q
# quotes for reuse, U u L change case, E expands escapes, A prints an
# assignment, and a lists the attribute letters.
v="a b"
echo "${v@Q}"
w=abc
echo "${w@Q}"
empty=""
echo "${empty@Q}"
q="a'b"
echo "${q@Q}"
esc=$'\e[1m'
echo "${esc@Q}"
m="Hello World"
echo "${m@U}"
echo "${m@L}"
echo "${m@u}"
e='a\tb\nc'
printf '%s' "${e@E}" | od -An -c | tr -s ' '
echo "${v@A}"
export EV=1
echo "${EV@a}"
readonly RV=1
echo "${RV@a}"
arr=(x y z)
echo "${arr@K}"
echo "${arr@k}"
p="end\$"
echo "${p@P}"
