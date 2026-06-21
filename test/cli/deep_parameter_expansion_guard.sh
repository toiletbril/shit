# A deeply nested parameter expansion default is capped with a located error
# rather than overflowing the native stack, so the shell reports the depth and
# stays alive. The depth here is past the parameter-expansion cap, while a
# shallow nesting still expands.
deep_open=$(printf '${x:-%.0s' $(seq 1 600))
deep_close=$(printf '}%.0s' $(seq 1 600))
deep="${deep_open}z${deep_close}"

echo "== deep nesting is guarded, not crashing:"
"$BIN" --mood bash -c "echo $deep" 2>&1 | grep -c "nested too deeply"

echo "== exit status is a clean error, not a signal death:"
"$BIN" --mood bash -c "echo $deep" >/dev/null 2>&1
status=$?
[ "$status" -lt 128 ] && echo ok || echo "crashed with signal $((status - 128))"

echo "== shallow nesting still expands:"
"$BIN" --mood bash -c 'echo "${x:-${y:-hi}}"'
