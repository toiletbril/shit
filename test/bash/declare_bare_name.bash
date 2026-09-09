#!/bin/bash
# A bare declare or local operand names a variable that has no value. The name
# stays visible to declare -p and to the bare listing until it is unset.
declare plainy
declare -i inty
declare -r ronly
readonly ronly2

declare -p plainy
echo "plainy_status=$?"
declare -p inty
declare -p ronly
declare -p ronly2

echo "expansion=${plainy-absent}"
echo "set_listing=$(set | /usr/bin/grep -c '^plainy')"

declare -p | /usr/bin/grep -E '^declare .. (plainy|inty|ronly|ronly2)$' | sort

unset plainy
declare -p plainy 2> /dev/null
echo "after_unset=$?"

scoped() {
  local loc
  declare -p loc
  declare -p | /usr/bin/grep -c '^declare -- loc$'
  declare deeper
  declare -p deeper
}
scoped

declare -p loc 2> /dev/null
echo "loc_outside=$?"
declare -p deeper 2> /dev/null
echo "deeper_outside=$?"

declare valued=1
declare -p valued
