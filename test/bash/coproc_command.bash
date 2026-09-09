#!/bin/bash

# The named form binds NAME[0], NAME[1], and NAME_PID. Raw descriptor numbers
# are never printed, because the numbers a shell picks are its own business.
echo "== named compound =="
coproc UPPER { while read -r line; do printf '%s\n' "${line}!"; done; }
printf '%s\n' "elements:${#UPPER[@]}"
printf '%s\n' "pid set:${UPPER_PID:+yes}"
printf '%s\n' "bang matches:$([ "$!" = "$UPPER_PID" ] && echo yes || echo no)"
printf 'one\n' >&"${UPPER[1]}"
read -r reply <&"${UPPER[0]}"
printf '%s\n' "reply:$reply"
printf 'two\n' >&"${UPPER[1]}"
read -r reply <&"${UPPER[0]}"
printf '%s\n' "reply:$reply"

# The read loop ends at end of file. The write descriptor is closed to reach it,
# otherwise the wait below never returns.
eval "exec ${UPPER[1]}>&-"
wait "$UPPER_PID"
printf '%s\n' "named wait status:$?"
printf '%s\n' "COPROC unset:${COPROC+set}"

echo "== unnamed simple =="
coproc cat
printf '%s\n' "elements:${#COPROC[@]}"
printf 'plain\n' >&"${COPROC[1]}"
read -r out <&"${COPROC[0]}"
printf '%s\n' "out:$out"
eval "exec ${COPROC[1]}>&-"
wait "$COPROC_PID"
printf '%s\n' "unnamed wait status:$?"

# A word names the coprocess only when a compound command follows it. Here the
# word is the command, and the coprocess takes the default name.
echo "== word that is not a name =="
NOTANAME() { printf 'ran:%s\n' "$*"; }
coproc NOTANAME cat
printf '%s\n' "elements:${#COPROC[@]}"
read -r line <&"${COPROC[0]}"
printf '%s\n' "line:$line"
wait "$COPROC_PID"
printf '%s\n' "word wait status:$?"

# The launch reports its own status. The status of the body is reached by wait.
echo "== status of the coproc command =="
coproc LAST { exit 7; }
printf '%s\n' "launch status:$?"
wait "$LAST_PID"
printf '%s\n' "body status:$?"

# A forked subshell loses both descriptors. The diagnostic of a failed
# redirection names the script, so it is dropped here.
echo "== descriptors in a subshell =="
coproc HOLD { while read -r line; do printf '%s\n' "got:$line"; done; }
( if printf 'x\n' 2>/dev/null >&"${HOLD[1]}"; then
    echo "subshell write:open"
  else
    echo "subshell write:closed"
  fi )
( if read -r probe 2>/dev/null <&"${HOLD[0]}"; then
    echo "subshell read:open value=$probe"
  else
    echo "subshell read:closed"
  fi )

# The array and the process id stay readable in the subshell. Only the
# descriptors behind them are gone.
( printf '%s\n' "elements in subshell:${#HOLD[@]}" )
( printf '%s\n' "pid in subshell:${HOLD_PID:+yes}" )

printf 'parent\n' >&"${HOLD[1]}"
read -r reply <&"${HOLD[0]}"
printf '%s\n' "parent reply:$reply"
eval "exec ${HOLD[1]}>&-"
wait "$HOLD_PID"
printf '%s\n' "subshell wait status:$?"

# An external program receives neither descriptor. The diagnostic of the failed
# redirection belongs to the child shell, so it is dropped here.
echo "== descriptors across an exec =="
coproc PASS { while read -r line; do printf '%s\n' "got:$line"; done; }
if /bin/sh -c "printf 'x\n' >&${PASS[1]}" 2>/dev/null; then
  echo "child write:open"
else
  echo "child write:closed"
fi
if /bin/sh -c "read -r probe <&${PASS[0]}" 2>/dev/null; then
  echo "child read:open"
else
  echo "child read:closed"
fi

eval "exec ${PASS[1]}>&-"
wait "$PASS_PID"
printf '%s\n' "exec wait status:$?"

# The descriptor variable form takes an array element. Each coprocess descriptor
# is closed here without an eval around a numeric close.
echo "== element close =="
coproc ELEM { read -r line; printf '%s\n' "element got:$line"; }
printf 'direct\n' >&"${ELEM[1]}"
exec {ELEM[1]}>&-
read -r element_reply <&"${ELEM[0]}"
exec {ELEM[0]}<&-
printf '%s\n' "$element_reply"
wait "$ELEM_PID"
printf '%s\n' "element wait status:$?"
