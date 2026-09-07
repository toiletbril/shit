unset KOSH_FLAGS
# -WWW reports an unset variable read in the rc. --no-init-diagnostics silences
# the startup stage while keeping -WWW for the session.
home=$(mktemp -d)
trap '[ -n "$home" ] && "$TEST_SYSTEM_RM" -rf -- "$home"' EXIT
printf 'echo "rc[${UNSET_IN_RC}]"\n' > "$home/.koshrc"
echo "== -WWW warns during init:"
HOME="$home" "$BIN" -WWW -i </dev/null 2>&1 | grep -c "is not set"
echo "== --no-init-diagnostics silences init:"
HOME="$home" "$BIN" -WWW --no-init-diagnostics -i </dev/null 2>&1 | grep -c "is not set"
echo "== -WWW stays active for the session:"
"$BIN" -WWW --no-init-diagnostics -c 'echo "[${UNSET_AT_PROMPT}]"' 2>&1 | grep -c "is not set"
printf 'set -o no-diagnostics\nset -o | while read -r name state; do case "$name:$state" in no-diagnostics:on) echo diagnostics-disabled=1 ;; esac; done\n' \
  > "$home/.koshrc"
echo "== a startup diagnostics change survives suppression:"
HOME="$home" "$BIN" --no-init-diagnostics -i </dev/null 2>&1 | \
  grep -c '^diagnostics-disabled=1$'
printf 'set -W\nset -o | while read -r name state; do case "$name:$state" in no-diagnostics:on) echo diagnostics-still-suppressed=1 ;; esac; done\n' \
  > "$home/.koshrc"
echo "== a startup warning change does not end suppression:"
HOME="$home" "$BIN" --no-init-diagnostics -i </dev/null 2>&1 | \
  grep -c '^diagnostics-still-suppressed=1$'
rm -f "$home/.koshrc"
printf 'set -WWW\n' > "$home/.profile"
echo "== a startup warning level reaches the session:"
HOME="$home" "$BIN" -l --no-init-diagnostics -c 'f() { echo "[${UNSET_AT_PROMPT}]"; }; f' 2>&1 | \
  grep -c "^1:12: warning: The variable 'UNSET_AT_PROMPT' is not set"
