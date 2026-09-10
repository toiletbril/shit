#!/bin/bash
# The readonly builtin accepts the indexed array, associative array, and
# function attribute letters.

readonly -a indexed=(5 6)
declare -p indexed

readonly -A mapped=([k]=v)
declare -p mapped

marked() { echo original; }
readonly -f marked

marked() { echo replaced; }
echo "redefine=$?"
marked

unset -f marked
echo "unset=$?"
marked

unset marked
echo "bare-unset=$?"
marked

declare -F marked
echo "still=$?"

readonly -f absent
echo "missing=$?"

plain() { echo plain; }
readonly -pf | /usr/bin/grep -e '^declare -fr'
echo "list=$?"
