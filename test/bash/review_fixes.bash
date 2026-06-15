#!/bin/bash
# Regression cover for the review fixes whose output bash defines exactly. The
# PIPESTATUS array reports each stage by position, and a negative array index
# counts back from the highest set index across the sparse elements rather than
# the dense length.
true | false | true
echo "${PIPESTATUS[@]}"
a=(A B)
a[5]=z
echo "[${a[-1]}][${a[-2]}]"
b=(x y z)
echo "${b[-1]} ${b[-2]} ${b[-3]}"
