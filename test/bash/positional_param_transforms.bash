#!/bin/bash
# A value-transform modifier maps over the positional parameters, the same way
# it maps over an array, checked byte-for-byte against bash. The case mods, the
# prefix and suffix removals, and the pattern replacement each run per element.
# A quoted star joins the transformed elements under the first IFS byte, while
# the at form keeps them as separate words.
set -- foo bar baz
echo "caret: ${@^}"
echo "caretcaret: ${@^^}"
echo "comma: ${@,,}"
echo "remove_prefix: ${@#b}"
echo "remove_suffix: ${@%z}"
echo "replace: ${@/a/X}"
echo "replace_all: ${@//a/X}"
echo "star_caret: ${*^^}"
IFS=-
echo "star_join: ${*^}"
unset IFS
count=0
for word in "${@^}"; do count=$((count + 1)); echo "elem$count=$word"; done
