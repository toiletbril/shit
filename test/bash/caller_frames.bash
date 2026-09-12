#!/bin/bash
# The caller builtin reads the same frame stacks FUNCNAME, BASH_SOURCE, and
# BASH_LINENO expose. A bare caller prints the call line and the source one
# frame above it, and prints NULL when no such source exists. A caller with a
# frame operand also prints the function name of the frame above. Every printed
# path is reduced to its base name, because the two shells receive the fixture
# through different path forms.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

trim() {
  trimmed=""
  for word in $1; do
    trimmed="$trimmed ${word##*/}"
  done
  echo "${trimmed# }"
}

printf '%s\n' \
  'inner() {' \
  '  trim "$(caller)"' \
  '  trim "$(caller 0)"' \
  '  trim "$(caller 1)"' \
  '  trim "$(caller 2)"' \
  '  caller 2 > /dev/null 2>&1' \
  '  echo "s2=$?"' \
  '  caller 3 > /dev/null 2>&1' \
  '  echo "s3=$?"' \
  '}' \
  'outer() {' \
  '  inner' \
  '}' \
  'outer' > "$dir/inner.sh"

echo "--- sourced frames ---"
. "$dir/inner.sh"

plain() {
  trim "$(caller)"
  trim "$(caller 0)"
  caller 1 > /dev/null 2>&1
  echo "s1=$?"
}

echo "--- script frames ---"
plain

echo "--- top level ---"
trim "$(caller)"
caller > /dev/null 2>&1
echo "bare=$?"
caller 0 > /dev/null 2>&1
echo "zero=$?"

echo "--- operand errors ---"
caller nope > /dev/null 2>&1
echo "word=$?"
caller -1 > /dev/null 2>&1
echo "negative=$?"
