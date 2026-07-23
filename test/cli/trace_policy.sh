unset SHIT_FLAGS
d=$(mktemp -d)
trap '[ -n "$d" ] && rm -rf "$d"' EXIT

printf 'no_such_named_xyz\n' > "$d/direct"
out=$("$BIN" "$d/direct" 2>&1)
printf 'named traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$(printf 'no_such_stdin_xyz\n' | "$BIN" 2>&1)
printf 'stdin traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$("$BIN" -c "eval 'no_such_eval_xyz'" 2>&1)
printf 'eval traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

recursive='if ((depth)); then let depth-=1; eval "$recursive"; else no_such_recursive_xyz; fi'
out=$("$BIN" -c 'recursive=$1; depth=5; eval "$recursive"' trace-driver "$recursive" 2>&1)
printf 'recursive traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$("$BIN" --no-traces -c "eval 'no_such_suppressed_xyz'" 2>&1)
printf 'disabled-runtime traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

printf '[[ x = "$UNSET_TRACE_POLICY" ]]\n' > "$d/warning"
out=$("$BIN" -W --no-traces -c '. "$1"' trace-driver "$d/warning" 2>&1)
printf 'disabled-warning traces=%s warnings=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')" \
    "$(printf '%s\n' "$out" | grep -c 'warning:')"

out=$("$BIN" --no-traces -c "eval '{'" 2>&1)
printf 'disabled-parse traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

printf "eval 'no_such_fallback_xyz'\n" > "$d/fallback"
chmod +x "$d/fallback"
out=$("$BIN" --no-traces -c '"$1"' trace-driver "$d/fallback" 2>&1)
printf 'disabled-fallback traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"
