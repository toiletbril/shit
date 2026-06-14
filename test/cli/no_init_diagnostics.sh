unset SHIT_FLAGS
# -W reports an unset variable read in the rc, and --no-init-diagnostics silences
# the startup stage while keeping -W for the session.
home=$(mktemp -d)
trap 'rm -rf "$home"' EXIT
printf 'echo "rc[${UNSET_IN_RC}]"\n' > "$home/.shitrc"
echo "== -W warns during init:"
HOME="$home" "$BIN" -W -i </dev/null 2>&1 | grep -c "is not set"
echo "== --no-init-diagnostics silences init:"
HOME="$home" "$BIN" -W --no-init-diagnostics -i </dev/null 2>&1 | grep -c "is not set"
echo "== -W stays active for the session:"
"$BIN" -W --no-init-diagnostics -c 'echo "[${UNSET_AT_PROMPT}]"' 2>&1 | grep -c "is not set"
