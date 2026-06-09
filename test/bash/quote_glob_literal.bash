#!/bin/bash
# Bash treats a glob metacharacter as literal when it is quoted or backslash
# escaped, so the word stays as written instead of matching a path. An unquoted
# pattern that matches nothing stays literal too under the default nullglob off,
# and these patterns are chosen to match nothing in any directory.
echo '*'
echo "?"
echo \[abc\]
echo '*.nomatchxyz'
echo "a*b?c[d]"
echo /no_such_dir_zzz/*
echo \*\?\[
printf '[%s]\n' '*'
