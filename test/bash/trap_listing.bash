#!/bin/bash
# shellcheck disable=SC2015,SC2034,SC2086,SC2249

echo ignored-debug
( set -T; trap '' DEBUG; echo body-1; echo body-2 )
echo after-ignored-debug=$?

echo ignored-debug-print
( set -T; trap '' DEBUG; trap -p DEBUG )
echo after-ignored-debug-print=$?

echo ignored-err
( set -E; trap '' ERR; false; echo after-false=$? )
echo after-ignored-err=$?

echo ignored-return
( set -T; trap '' RETURN
  f() { echo in-f; }
  f
  trap -p RETURN )
echo after-ignored-return=$?

echo printed-debug
( trap 'echo "D-[$BASH_COMMAND]"' DEBUG; trap -p DEBUG )
echo after-printed-debug=$?

echo printed-err
( trap 'echo E-err' ERR; trap -p ERR )
echo after-printed-err=$?

echo printed-return
( trap 'echo R-return' RETURN; trap -p RETURN )
echo after-printed-return=$?

echo printed-all
( trap 'echo D' DEBUG; trap 'echo E' ERR; trap 'echo R' RETURN; trap -p )
echo after-printed-all=$?

echo round-trip
( trap 'echo D-round' DEBUG
  saved=$(trap -p DEBUG)
  trap - DEBUG
  eval "$saved"
  set -T
  echo round-target
  trap - DEBUG )
echo after-round-trip=$?

echo restored-debug
( set -T; trap '' DEBUG; trap - DEBUG; echo restored-body )
echo after-restored-debug=$?

echo order-mixed
( trap 'echo R' RETURN
  trap 'echo T' TERM
  trap 'echo D' DEBUG
  trap 'echo X' EXIT
  trap 'echo I' INT
  trap 'echo E' ERR
  trap 'echo H' HUP
  trap -p ) </dev/null
echo after-order-mixed=$?

echo order-signals-only
( trap 'echo T' TERM; trap 'echo I' INT; trap 'echo H' HUP; trap 'echo U' USR1; trap -p )
echo after-order-signals-only=$?

echo filter-order
( trap 'echo R' RETURN; trap 'echo D' DEBUG; trap 'echo E' ERR; trap -p ERR RETURN DEBUG )
echo after-filter-order=$?

echo filter-repeated
( trap 'echo D' DEBUG; trap -p DEBUG DEBUG )
echo after-filter-repeated=$?

echo filter-unset
( trap 'echo D' DEBUG; trap -p DEBUG TERM ERR )
echo after-filter-unset=$?

echo filter-invalid
( trap 'echo D' DEBUG; trap -p NOSUCHSIGNAL DEBUG ) 2>/dev/null
echo after-filter-invalid=$?

echo filter-numeric
( trap 'echo X' EXIT; trap 'echo T' TERM; trap -p 15 0 )
echo after-filter-numeric=$?

# The listing wraps every action in single quotes. A bare word is wrapped as
# well, and it survives the round trip back through eval.
echo quoted-bare-action
( trap true DEBUG; trap -p DEBUG )
echo after-quoted-bare-action=$?

echo quoted-embedded-quote
( trap 'echo it'\''s here' USR1; trap -p USR1 )
echo after-quoted-embedded-quote=$?

echo quoted-dollar-action
( trap 'echo $unset_name' USR2; trap -p USR2 )
echo after-quoted-dollar-action=$?

echo quoted-round-trip
( trap true USR1
  saved=$(trap -p USR1)
  trap - USR1
  eval "$saved"
  trap -p USR1 )
echo after-quoted-round-trip=$?

# An empty listing prints nothing and reports success in both forms.
echo empty-listing
( trap; echo empty-listing-status=$? )
echo after-empty-listing=$?

echo empty-print-listing
( trap -p; echo empty-print-listing-status=$? )
echo after-empty-print-listing=$?

# A separator ends the option list. The word after it is the action, and a
# lone separator sets nothing.
echo separator-set
( trap -- 'echo H' HUP; echo separator-set-status=$?; trap -p HUP )
echo after-separator-set=$?

echo separator-only
( trap --; echo separator-only-status=$? ) 2>/dev/null
echo after-separator-only=$?

echo separator-reset
( trap 'echo H' HUP; trap -- - HUP; echo separator-reset-status=$?; trap -p HUP )
echo after-separator-reset=$?

# A single operand resets the condition. An unknown name reports 2 in that
# form and 1 in every form that names more than one condition.
echo reset-valid-single
( trap 'echo H' HUP; trap HUP; echo reset-valid-single-status=$?; trap -p HUP )
echo after-reset-valid-single=$?

echo reset-invalid-single
( trap NOSUCHSIGNAL; echo reset-invalid-single-status=$? ) 2>/dev/null
echo after-reset-invalid-single=$?

echo multi-reset
( trap 'echo U' USR1 USR2; trap - USR1 USR2; echo multi-reset-status=$?; trap -p USR1 USR2 )
echo after-multi-reset=$?

echo multi-reset-invalid
( trap 'echo H' HUP; trap - HUP NOSUCHSIGNAL; echo multi-reset-invalid-status=$?; trap -p HUP ) 2>/dev/null
echo after-multi-reset-invalid=$?

# An unknown name in a set form leaves the valid conditions installed.
echo multi-set-invalid
( trap 'echo N' HUP NOSUCHSIGNAL; echo multi-set-invalid-status=$?; trap -p HUP ) 2>/dev/null
echo after-multi-set-invalid=$?

# The numbers past the fifth entry differ between systems, so only the layout
# of the first line and a few fixed entries are asserted.
echo signal-list
signal_table=$(trap -l)
first_line=${signal_table%%$'\n'*}
echo "first-line=[$first_line]"
column_count=0
for field in $first_line; do
  case $field in
    *")") column_count=$((column_count + 1)) ;;
  esac
done
echo "first-line-columns=$column_count"
case $signal_table in
  " 1) SIGHUP"*) echo padded-first-number=yes ;;
  *) echo padded-first-number=no ;;
esac
case $signal_table in
  *"15) SIGTERM"*) echo has-sigterm=yes ;;
  *) echo has-sigterm=no ;;
esac
case $signal_table in
  *"13) SIGPIPE"*) echo has-sigpipe=yes ;;
  *) echo has-sigpipe=no ;;
esac
echo "kill-list-agrees=$([ "$(kill -l)" = "$signal_table" ] && echo yes || echo no)"
echo after-signal-list=$?

echo trap-listing-done
