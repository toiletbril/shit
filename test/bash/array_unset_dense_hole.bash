#!/bin/bash
# Unset of a dense array element leaves a hole rather than renumbering the later
# indices, so the surviving indices, the values, and a re-assignment into the
# hole all match bash.
a=(a b c d)
unset 'a[1]'
echo "${!a[@]}"
echo "${a[@]}"
echo "${a[2]}"
a[1]=X
echo "${!a[@]}"
echo "${a[@]}"
unset 'a[0]'
echo "${!a[@]}"
echo "${a[@]}"
