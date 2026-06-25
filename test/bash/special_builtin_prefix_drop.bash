#!/bin/bash
# In the bash mood a prefix assignment before a special builtin is dropped after
# the command, unlike the POSIX persistence, checked byte-for-byte against bash.
# An exported assignment from the prefix still reaches the environment for the
# command's duration, so the colon's exported name survives while the prefix
# name does not.
x=before
x=after :
echo "colon=[$x]"
y=before
y=after export z=hi
echo "export=[$y][$z]"
w=before
w=after eval ':'
echo "eval=[$w]"
