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
