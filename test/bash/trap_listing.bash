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

echo trap-listing-done
