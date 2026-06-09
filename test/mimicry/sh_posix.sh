#!/bin/sh
# An sh shebang makes shit mimic a POSIX shell, matched against dash.
x=5
if [ "$x" = 5 ]; then echo equal; fi
for i in a b c; do printf '%s' "$i"; done; echo
echo "len=${#x}"
case "$x" in 5) echo five;; *) echo other;; esac
