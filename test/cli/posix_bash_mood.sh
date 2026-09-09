unset KOSH_FLAGS
# --posix selects the bash-posix mood, not the dash-like sh mood, so the bash
# identity is seeded and the bash grammar stays on. [[ still works, arrays still
# work, and BASH_VERSION is set so a bash rc detects it.
echo "== --posix keeps [[ working:"
"$BIN" --posix -c '[[ x == x ]] && echo bracket-works'
echo "== --posix seeds BASH_VERSION:"
"$BIN" --posix -c 'echo "BASH_VERSION=${BASH_VERSION:-unset}"'
echo "== --posix seeds BASH_VERSINFO:"
"$BIN" --posix -c \
  'printf "BASH_VERSINFO=%s.%s.%s.%s\n" "${BASH_VERSINFO[@]:0:4}"'
echo "== --posix reports the bash-posix mood:"
"$BIN" --posix -c 'set --mood'
echo "== --mood bash-posix keeps [[ working:"
"$BIN" --mood bash-posix -c '[[ x == x ]] && echo bracket-works'
echo "== --posix does not enter the sh mood:"
"$BIN" --posix -c 'arr=(a b c); echo "${arr[1]}"'
# The sh mood rejects DEBUG, ERR, and RETURN. The bash-posix mood keeps all
# three, and RETURN follows the bash rule of firing under functrace and for a
# body that installs an action for itself.
echo "== --posix accepts the DEBUG condition:"
"$BIN" --posix -c 'trap "echo D" DEBUG; echo traced; trap - DEBUG'
echo "== --posix accepts the ERR condition:"
"$BIN" --posix -c 'trap "echo E" ERR; false; echo after-false'
echo "== --posix accepts the RETURN condition under functrace:"
"$BIN" --posix -c \
  'f() { echo body; }; trap "echo R" RETURN; f; set -T; f; trap - RETURN'
echo "== --posix fires RETURN for a body that installs its own action:"
"$BIN" --posix -c 'g() { trap "echo own" RETURN; echo g-body; }; g; echo after-g'
echo "== --posix lists all three conditions:"
"$BIN" --posix -c \
  'trap "echo E" ERR; trap "echo R" RETURN; trap "echo D" DEBUG; trap -p'
