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

# Dash accepts no trap option. Each option form ends the subshell with the
# status a special builtin reports for a usage error, so every case runs in
# its own subshell and the parent reads the status back.
echo print-form
( trap 'echo U-fired' USR1; trap -p USR1; echo print-form-reached=$? ) 2>/dev/null
echo print-form-status=$?

echo list-form
( trap -l; echo list-form-reached=$? ) 2>/dev/null
echo list-form-status=$?

echo unknown-option
( trap -x USR1; echo unknown-option-reached=$? ) 2>/dev/null
echo unknown-option-status=$?

echo joined-option
( trap -HUP; echo joined-option-reached=$? ) 2>/dev/null
echo joined-option-status=$?

# A lone dash and the separator are operands, not options.
echo lone-dash
( trap -; echo lone-dash-set=$? ) 2>/dev/null
echo lone-dash-status=$?

echo separator-only
( trap --; echo separator-only-set=$? ) 2>/dev/null
echo separator-only-status=$?

echo separator-protects-action
( trap -- -p HUP; echo separator-protects-action-set=$?; trap ) 2>/dev/null
echo separator-protects-action-status=$?

# An option shaped word past the action is a condition name.
echo option-after-action
( trap '' -HUP; echo option-after-action-set=$? ) 2>/dev/null
echo option-after-action-status=$?

echo set-exit
trap 'echo X-fired' EXIT 2>/dev/null
echo set-exit-status=$?

echo trap-posix-conditions-done
