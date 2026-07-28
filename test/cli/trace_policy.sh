unset SHIT_FLAGS
d=$(mktemp -d)
trap '[ -n "$d" ] && rm -rf "$d"' EXIT

printf 'no_such_named_xyz\n' > "$d/direct"
out=$("$BIN" "$d/direct" 2>&1)
printf 'named traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$(printf 'no_such_stdin_xyz\n' | "$BIN" 2>&1)
printf 'stdin traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$("$BIN" -c "eval 'no_such_eval_xyz'" 2>&1)
printf 'eval traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$("$BIN" -c no_such_first_root_xyz -c no_such_second_root_xyz 2>&1)
printf 'multi-root traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"
printf 'multi-root first=%s second=%s\n' \
    "$(printf '%s\n' "$out" | grep -c 'no_such_first_root_xyz')" \
    "$(printf '%s\n' "$out" | grep -c 'no_such_second_root_xyz')"

out=$("$BIN" -n -WW \
    -c 'echo "$TRACE_FIRST"; TRACE_FIRST=x; echo "$TRACE_SECOND"; TRACE_SECOND=x' \
    -c : 2>&1)
printf 'noexec-batch traces=%s warnings=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'warning:')"

recursive='if ((depth)); then let depth-=1; eval "$recursive"; else no_such_recursive_xyz; fi'
out=$("$BIN" -c 'recursive=$1; depth=5; eval "$recursive"' trace-driver "$recursive" 2>&1)
printf 'recursive traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$("$BIN" --mood bash -c 'eval "$1"' root 'eval "$2"' '?????????' 2>&1)
warning_block=$(printf '%s\n' "$out" | sed '/error:/,$d')
printf 'runtime-repeat traces=%s warnings=%s\n' \
    "$(printf '%s\n' "$warning_block" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$warning_block" | grep -c 'warning:')"

printf '%s\n' 'current=$NEXT' 'NEXT=$AFTER' '. "$current"' > "$d/identity-a"
printf '%s\n' 'current=$NEXT' 'NEXT=$AFTER' '. "$current"' > "$d/identity-b"
printf 'no_such_identity_xyz\n' > "$d/identity-c"
out=$(NEXT="$d/identity-b" AFTER="$d/identity-c" \
    "$BIN" -c '. "$1"' trace-driver "$d/identity-a" 2>&1)
printf 'identity traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$("$BIN" --no-traces -c "eval 'no_such_suppressed_xyz'" 2>&1)
printf 'disabled-runtime traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$("$BIN" --no-traces -c no_such_first_root_xyz -c no_such_second_root_xyz 2>&1)
printf 'disabled-multi-root traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

printf '[[ x = "$UNSET_TRACE_POLICY" ]]\n' > "$d/warning"
out=$("$BIN" -W --no-traces -c '. "$1"' trace-driver "$d/warning" 2>&1)
printf 'disabled-warning traces=%s warnings=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'warning:')"

out=$("$BIN" --no-traces -c "eval '{'" 2>&1)
printf 'disabled-parse traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

printf "eval 'no_such_fallback_xyz'\n" > "$d/fallback"
chmod +x "$d/fallback"
out=$("$BIN" -c '"$1"' trace-driver "$d/fallback" 2>&1)
printf 'enabled-fallback traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

printf '{\n' > "$d/fallback-parse"
chmod +x "$d/fallback-parse"
out=$("$BIN" -c '"$1"' trace-driver "$d/fallback-parse" 2>&1)
printf 'enabled-fallback-parse traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"

out=$("$BIN" --no-traces -c '"$1"' trace-driver "$d/fallback" 2>&1)
printf 'disabled-fallback traces=%s errors=%s\n' \
    "$(printf '%s\n' "$out" | grep -Ec 'trace:')" \
    "$(printf '%s\n' "$out" | grep -c 'error:')"
