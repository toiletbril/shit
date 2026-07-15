echo "== set --m:"
"$BIN" --debug-complete-at 'set --m' </dev/null
echo "== set --i:"
"$BIN" --debug-complete-at 'set --i' </dev/null
echo "== set --mood value:"
"$BIN" --debug-complete-at 'set --mood ' </dev/null
echo "== set --init-moods value:"
"$BIN" --debug-complete-at 'set --init-moods ' </dev/null
echo "== set -M value:"
"$BIN" --debug-complete-at 'set -M ' </dev/null
echo "== set -L value:"
"$BIN" --debug-complete-at 'set -L ' </dev/null
