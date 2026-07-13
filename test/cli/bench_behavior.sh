unset SHIT_FLAGS
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
    perf stat -e cycles -- /bin/true >/dev/null 2>&1; then
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
