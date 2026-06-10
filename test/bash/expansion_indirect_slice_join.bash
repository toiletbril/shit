#!/bin/bash
# Indirect expansion carries its modifier to the target, positional slices
# yield fields, the [*] join uses the first IFS byte, and a quoted slash stays
# inside a replacement pattern.
x=0; v=x
echo "[${!v+set}]" "[${!v-fb}]"
unset x
echo "[${!v+set}]" "[${!v:-empty}]"
f() { printf "[%s]" "${@:2}"; echo; printf "[%s]" "${@: -1}"; echo; echo "[${*:2}]"; }
f a "b b" c
g() { local -a arr=(); arr+=("${@:2}"); echo "n=${#arr[@]} first=${arr[0]}"; }
g x y z
lines=(l1 l2 l3)
IFS=$'\n'
joined="${lines[*]}"
printf '%s' "$joined" | od -An -c | tr -s ' '
unset IFS
def="g/h:i () X"
echo "[${def/#"g/h:i"/"copy:g"}]"
p="a/b/c"; echo "[${p//"/"/_}]"
