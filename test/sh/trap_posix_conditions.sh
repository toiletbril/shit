#!/bin/sh
# shellcheck disable=SC2249

echo set-debug
trap 'echo D-fired' DEBUG 2>/dev/null
echo set-debug-status=$?
echo debug-body

echo set-err
trap 'echo E-fired' ERR 2>/dev/null
echo set-err-status=$?
false
echo err-body=$?

echo set-return
trap 'echo R-fired' RETURN 2>/dev/null
echo set-return-status=$?
plain_function() { echo function-body; }
plain_function
echo return-body=$?

echo reset-debug
trap DEBUG 2>/dev/null
echo reset-debug-status=$?

echo reset-err
trap ERR 2>/dev/null
echo reset-err-status=$?

echo reset-return
trap RETURN 2>/dev/null
echo reset-return-status=$?

echo set-exit
trap 'echo X-fired' EXIT 2>/dev/null
echo set-exit-status=$?

echo trap-posix-conditions-done
