unset KOSH_FLAGS
# --posix selects the bash-posix mood, not the dash-like sh mood, so the bash
# identity is seeded and the bash grammar stays on. [[ still works, arrays still
# work, and BASH_VERSION is set so a bash rc detects it.
echo "== --posix keeps [[ working:"
"$BIN" --posix -c '[[ x == x ]] && echo bracket-works'; echo "rc=$?"
echo "== --posix seeds BASH_VERSION:"
"$BIN" --posix -c 'echo "BASH_VERSION=${BASH_VERSION:-unset}"'; echo "rc=$?"
echo "== --posix seeds BASH_VERSINFO:"
"$BIN" --posix -c \
  'printf "BASH_VERSINFO=%s.%s.%s.%s\n" "${BASH_VERSINFO[@]:0:4}"'; echo "rc=$?"
echo "== --posix reports the bash-posix mood:"
"$BIN" --posix -c 'set --mood'; echo "rc=$?"
echo "== --mood bash-posix keeps [[ working:"
"$BIN" --mood bash-posix -c '[[ x == x ]] && echo bracket-works'; echo "rc=$?"
echo "== --posix does not enter the sh mood:"
"$BIN" --posix -c 'arr=(a b c); echo "${arr[1]}"'; echo "rc=$?"
# The sh mood rejects DEBUG, ERR, and RETURN. The bash-posix mood keeps all
# three, and RETURN follows the bash rule of firing under functrace and for a
# body that installs an action for itself.
echo "== --posix accepts the DEBUG condition:"
"$BIN" --posix -c 'trap "echo D" DEBUG; echo traced; trap - DEBUG'; echo "rc=$?"
echo "== --posix accepts the ERR condition:"
"$BIN" --posix -c 'trap "echo E" ERR; false; echo after-false'; echo "rc=$?"
echo "== --posix accepts the RETURN condition under functrace:"
"$BIN" --posix -c \
  'f() { echo body; }; trap "echo R" RETURN; f; set -T; f; trap - RETURN'
echo "rc=$?"
echo "== --posix fires RETURN for a body that installs its own action:"
"$BIN" --posix -c 'g() { trap "echo own" RETURN; echo g-body; }; g; echo after-g'
echo "rc=$?"
echo "== --posix lists all three conditions:"
"$BIN" --posix -c \
  'trap "echo E" ERR; trap "echo R" RETURN; trap "echo D" DEBUG
   trap -p DEBUG ERR RETURN'
echo "rc=$?"
# A sourced file fires the RETURN trap in the bash-posix mood, the same way the
# reference shell fires it under set -o posix. Only the sh mood rejects the
# condition.
posix_directory=$(mktemp -d)
trap '[ -n "$posix_directory" ] && /bin/rm -rf "$posix_directory"' EXIT
printf 'echo inner-body\n' > "$posix_directory/inner.sh"
echo "== --posix fires RETURN when a sourced file finishes:"
"$BIN" --posix -c 'trap "echo R-src" RETURN; . "$1"; echo "after=$?"' \
  posix-driver "$posix_directory/inner.sh"
echo "rc=$?"
echo "== --mood bash-posix fires RETURN when a sourced file finishes:"
"$BIN" --mood bash-posix -c 'trap "echo R-src" RETURN; . "$1"; echo "after=$?"' \
  posix-driver "$posix_directory/inner.sh"
echo "rc=$?"
echo "== the sh mood rejects RETURN for a sourced file:"
"$BIN" --mood sh -c 'trap "echo R-src" RETURN; . "$1"; echo "after=$?"' \
  posix-driver "$posix_directory/inner.sh" 2>&1
echo "rc=$?"
