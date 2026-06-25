#!/bin/bash
# $_ reads the last argument of the previous simple command, checked
# byte-for-byte against bash.
true alpha beta
echo "after_true=[$_]"
echo one two three
echo "after_echo=[$_]"
:
echo "after_colon=[$_]"
printf '%s\n' x y z
echo "after_printf=[$_]"
