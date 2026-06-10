#!/bin/bash
# unset removes a sparse array element held in the sparse map, not only a dense
# one, so the index disappears from the key list and the value list shrinks.
a=([0]=x [5]=y [10]=z)
echo "${!a[@]}"
echo "${a[@]}"
unset 'a[5]'
echo "${!a[@]}"
echo "${a[@]}"
unset 'a[10]'
echo "${!a[@]}"
echo "${a[@]}"
