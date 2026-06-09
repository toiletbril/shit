#!/bin/bash
# Array assignments given as arguments to the assignment builtins local and
# declare, checked byte-for-byte against bash. The elements field split and glob
# the way a command's arguments do, a local array does not leak to the caller,
# and a local array shadows then restores the caller's array.
f() {
  local -r editor=(a b c)
  printf '<%s>' "${editor[@]}"
  echo
  local versions=($(echo one two three))
  echo "${#versions[@]} ${versions[1]}"
}
f
echo "after f: [${editor[@]}]"
outer=(x y z)
g() { local outer=(inner); echo "in g: ${outer[@]}"; }
g
echo "after g: ${outer[@]}"
declare top=(p q r)
echo "${top[@]}"
