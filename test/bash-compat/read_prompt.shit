#!/bin/bash
# Bash read -p prompt, checked byte-for-byte against bash. The prompt goes to
# standard error and only when reading from a terminal, so a piped read shows no
# prompt and stdout carries only the result.
echo "world" | { read -p "name: " x; echo "hello $x"; }
echo "a b c" | { read -p "vals: " -a arr; echo "${arr[0]}-${arr[2]}"; }
printf 'value\n' | { read -r -p "P> " line; echo "[$line]"; }
echo "one two" | { read -p "two: " first second; echo "$second $first"; }
