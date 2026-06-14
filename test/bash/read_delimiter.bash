#!/bin/bash
# Bash read -d delimiter, checked byte-for-byte against bash. An empty delimiter
# reads until a NUL byte, so the whole input is slurped and split on IFS, the
# form a bash-completion script uses to load a compgen run into an array. A
# non-empty delimiter stops the read at its first byte.
printf 'a\nb\nc\n' | { IFS=$'\n' read -r -d '' -a arr; echo "${#arr[@]}:${arr[0]}:${arr[2]}"; }
read -r -d : a b <<< 'foo:bar:baz'
echo "[$a][$b]"
printf 'x y z:rest\n' | { read -r -d : first; echo "got=$first"; }
IFS=$'\n' read -r -d '' -a lines < <(printf 'one\ntwo\nthree\n')
echo "${#lines[@]}:${lines[1]}"
