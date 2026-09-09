#!/bin/bash
# The -p flag of an assignment builtin given a NAME=(...) operand, checked
# byte-for-byte against bash. The assignment lands first, and declare and local
# then print the reusable declaration while readonly and export print nothing.

echo declare-print-array-operand
declare -p d=(1 2)
echo rc=$?
echo "d=${d[*]}"

echo declare-print-array-append
declare -p d+=(3 4)
echo rc=$?
echo "d=${d[*]}"

echo declare-print-two-arrays
declare -p e=(a b) f=(c)
echo rc=$?
echo "e=${e[*]} f=${f[*]}"

echo declare-print-array-with-attributes
declare -pr g=(x y)
echo rc=$?
echo "g=${g[*]}"

echo declare-print-scalar-operand
declare -p missing_scalar=1
echo rc=$?
echo "missing_scalar=${missing_scalar-unset}"

echo local-print-array-operand
takes_local() {
  local -p h=(7 8)
  echo rc=$?
  echo "inner-h=${h[*]}"
}
takes_local
echo "outer-h=${h[*]-unset}"

echo readonly-print-array-operand
readonly -p j=(1 2)
echo rc=$?
echo "j=${j[*]}"

echo export-print-array-operand
export -p k=(3 4)
echo rc=$?
echo "k=${k[*]}"

echo declare-print-empty-array
declare -p m=()
echo rc=$?
echo "m-count=${#m[@]}"

echo declare-print-array-operand-done
