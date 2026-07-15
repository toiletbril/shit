unset SHIT_FLAGS
d=$(mktemp -d)
trap 'test -n "$d" && /bin/rm -rf "$d"' EXIT
# bench forks a shit process per sample and runs the command under it. Only the
# deterministic lines are checked here, since the timing summary varies per run.
echo "== bench stops on a non-zero exit code:"
"$BIN" -c 'bench --runs 5 false' 2>&1 | grep -E 'exited with status|note:'
echo "== the failure sets the exit status:"
"$BIN" -c 'bench --runs 5 false' >/dev/null 2>&1; echo "rc=$?"
echo "== --ignore-exit-code keeps sampling a failing command:"
"$BIN" -c 'bench --runs 3 --ignore-exit-code false' 2>&1 | grep 'Benchmark:'
echo "== --no-shell --show-output forks the command directly:"
"$BIN" -c 'bench --runs 2 --no-shell --show-output "/bin/echo direct"' 2>&1 | grep -E '^direct$|Benchmark:'
echo "== --runs executes exactly one sample:"
"$BIN" -c 'bench --runs 1 --ignore-exit-code true' 2>&1 | grep 'Benchmark:'
echo "== invalid sample counts are rejected:"
for count in 0 100001 18446744073709551616; do
    "$BIN" -c "bench --runs '$count' true" >/dev/null 2>&1
    echo "rc=$?"
done
echo "== unsigned overflow is rejected by number parsing:"
"$BIN" -c 'bench --runs 18446744073709551616 true' 2>&1 |
    grep 'expects a number, got'
echo "== invalid values carry granular carets:"
"$BIN" -c 'bench --runs nope true' 2>&1
"$BIN" -c 'bench --runs=nope true' 2>&1
"$BIN" -c 'bench --runs 0 true' 2>&1
"$BIN" -c 'bench --duration 18446744073710 true' 2>&1
"$BIN" -c 'bench "--runs=nope" true' 2>&1
"$BIN" -c 'runs=nope; bench --runs=$runs true' 2>&1
echo "== an overflowing duration is rejected:"
"$BIN" -c 'bench --duration 18446744073710 true' >/dev/null 2>&1
echo "rc=$?"
echo "== counter capability keeps one complete sample:"
counter_output=$("$BIN" -c 'bench --runs 1 --no-shell --show-output "/bin/echo counter-run"' 2>&1)
test "$(printf '%s\n' "$counter_output" | grep -c '^counter-run$')" -eq 1
counter_row_count=$(printf '%s\n' "$counter_output" | grep -Ec '^  (cpu cycles|instructions|cache refs|cache misses|branch misses)')
case "$counter_row_count" in
    0) ;;
    5)
        printf '%s\n' "$counter_output" | grep '^  cpu cycles' | grep -q '[1-9]'
        printf '%s\n' "$counter_output" | grep '^  instructions' | grep -q '[1-9]'
        ;;
    *) exit 1 ;;
esac
if [ "$(uname -s)" = Linux ] && command -v perf >/dev/null 2>&1 &&
    perf stat -e cycles,instructions,cache-references,cache-misses,branch-misses \
        -- /bin/true >/dev/null 2>&1; then
    test "$counter_row_count" -eq 5
fi
printf '%s\n' "$counter_output" | grep -q 'Benchmark: /bin/echo counter-run (1 runs)'
echo "counter capability passed"
echo "== counter fallback keeps one complete sample:"
if [ "$(uname -s)" = Linux ]; then
    fallback_output=$(
        ulimit -n 8
        "$BIN" -c 'bench --runs 1 --no-shell --show-output "/bin/echo fallback-run"' 2>&1
    )
    test "$(printf '%s\n' "$fallback_output" | grep -c '^fallback-run$')" -eq 1
    test "$(printf '%s\n' "$fallback_output" | grep -Ec '^  (cpu cycles|instructions|cache refs|cache misses|branch misses)')" -eq 0
    printf '%s\n' "$fallback_output" | grep -q 'Benchmark: /bin/echo fallback-run (1 runs)'
fi
echo "counter fallback passed"
echo "== a failed later sample clears terminal progress:"
cat > "$d/vanishing-command" <<'SH'
#!/bin/sh
/bin/sleep 0.03
/bin/rm -f "$0"
SH
chmod +x "$d/vanishing-command"
has_typescript=0
if script -qec true /dev/null >/dev/null 2>&1; then
    has_typescript=1
    NO_COLOR= TERM=xterm BIN=$BIN script -qec \
        "$BIN -c 'bench --runs 3 --no-shell $d/vanishing-command'" \
        "$d/typescript" >/dev/null 2>&1
elif script -q /dev/null /usr/bin/true >/dev/null 2>&1; then
    has_typescript=1
    NO_COLOR= TERM=xterm BIN=$BIN script -q "$d/typescript" "$BIN" -c \
        "bench --runs 3 --no-shell $d/vanishing-command" \
        >/dev/null 2>&1
else
    : > "$d/typescript"
fi
terminal_hex=$(od -An -tx1 "$d/typescript" | tr -d ' \n')
if [ "$has_typescript" -eq 1 ]; then
    [ -n "$terminal_hex" ] || exit 1
    case $terminal_hex in
        *0d1b5b324b736869743a*) ;;
        *) echo "progress clobbered error"; exit 1 ;;
    esac
fi
cat > "$d/progress-driver" <<'SH'
#!/bin/sh
exec "$BIN" -c 'bench --runs 2 "sleep 0.12 # deliberately long benchmark label" "sleep 0.12"'
SH
chmod +x "$d/progress-driver"
if script -qec true /dev/null >/dev/null 2>&1; then
    NO_COLOR= TERM=xterm BIN=$BIN script -qec "$d/progress-driver" \
        "$d/progress-typescript" >/dev/null 2>&1
elif script -q /dev/null /usr/bin/true >/dev/null 2>&1; then
    NO_COLOR= TERM=xterm BIN=$BIN script -q "$d/progress-typescript" \
        "$d/progress-driver" >/dev/null 2>&1
fi
if [ -e "$d/progress-typescript" ]; then
    progress_hex=$(od -An -tx1 "$d/progress-typescript" | tr -d ' \n')
    clear_count=$(printf '%s\n' "$progress_hex" | grep -o '0d1b5b324b' | \
        wc -l | tr -d ' ')
    [ "$clear_count" -ge 4 ] || exit 1
fi
echo "progress clear check complete"
