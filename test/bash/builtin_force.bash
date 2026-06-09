#!/bin/bash
# Bash builtin keyword forcing the builtin past a same-named function, checked
# byte-for-byte against bash.
echo() { command echo "wrapped: $*"; }
builtin echo direct call
echo via function
unset -f echo
builtin true; echo "true rc=$?"
builtin false; echo "false rc=$?"
