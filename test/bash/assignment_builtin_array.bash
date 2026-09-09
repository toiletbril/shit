#!/bin/bash
# An array literal given to readonly, export, declare, or local, checked
# byte-for-byte against bash. The parser lifts NAME=(...) out of the argument
# vector, and the builtin must still treat the invocation as one that received
# an operand.

echo readonly-array
( readonly ro=(1 2 3)
  echo "ro-all=${ro[*]}"
  echo "ro-one=${ro[1]}"
  echo "ro-count=${#ro[@]}" )
echo after-readonly-array=$?

echo readonly-print-flag
( readonly -p rp=(4 5)
  echo "rp-all=${rp[*]}" )
echo after-readonly-print-flag=$?

echo export-array
( export xp=(6 7)
  echo "xp-all=${xp[*]}"
  echo "xp-one=${xp[0]}" )
echo after-export-array=$?

echo export-print-flag-with-scalar
( export -p xs=8
  echo "xs=$xs" )
echo after-export-print-flag-with-scalar=$?

echo declare-array
( declare dc=(9 10)
  echo "dc-all=${dc[*]}"
  echo "dc-count=${#dc[@]}" )
echo after-declare-array=$?

echo declare-array-with-flag
( declare -a da=(11 12)
  echo "da-all=${da[*]}" )
echo after-declare-array-with-flag=$?

echo local-array
( takes_local() {
    local lc=(13 14)
    echo "lc-all=${lc[*]}"
    echo "lc-one=${lc[1]}"
  }
  takes_local
  echo "lc-after=[${lc[*]}]" )
echo after-local-array=$?

echo local-array-with-flag
( takes_local_flag() {
    local -a lf=(15 16)
    echo "lf-all=${lf[*]}"
  }
  takes_local_flag )
echo after-local-array-with-flag=$?

echo readonly-array-single
( readonly ra=(17)
  echo "ra-all=${ra[*]}"
  echo "ra-count=${#ra[@]}" )
echo after-readonly-array-single=$?

echo assignment-builtin-array-done
