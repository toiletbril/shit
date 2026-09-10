#!/bin/bash
# The FUNCNAME and BASH_SOURCE stacks of a file sourced from inside a function,
# and the BASH_LINENO call line of a function reached through another file.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

printf '%s\n' \
  'report() {' \
  '  echo "FUNCNAME=[${FUNCNAME[*]}]"' \
  '  echo "SOURCE=[${BASH_SOURCE[*]##*/}]"' \
  '  echo "depth=${#FUNCNAME[@]} ${#BASH_SOURCE[@]}"' \
  '}' \
  'report' > "$dir/inner.sh"

printf '%s\n' \
  'lines() {' \
  '  echo "LINE=[${BASH_LINENO[*]}]"' \
  '}' > "$dir/lib.sh"

printf '%s\n' 'nested' > "$dir/mid.sh"

outer() {
  source "$dir/inner.sh"
}

outer

echo "--- top level ---"
source "$dir/inner.sh"

echo "--- after ---"
echo "FUNCNAME=[${FUNCNAME[*]}]"
echo "SOURCE=[${BASH_SOURCE[*]##*/}]"
echo "depth=${#FUNCNAME[@]} ${#BASH_SOURCE[@]}"

echo "--- call lines ---"
source "$dir/lib.sh"

nested() {
  lines
}

carrier() {
  source "$dir/mid.sh"
}

carrier
