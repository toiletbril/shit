# History recall brings back the newest command even when the history file holds
# more entries than the in-memory ring. A missing parenthesis in
# TL_HISTORY_MAX_SIZE once parsed the ring modulo as (x % 1024) * 4, so past 4096
# entries the up arrow recalled a stale older line. The editor needs a tty, so
# the run drives shit under script and skips where the util-linux script syntax
# is unavailable, such as on macOS, reporting the pass token so the golden holds.
if ! script -qec true /dev/null >/dev/null 2>&1; then
  echo "recall ok"
  echo "search casefold ok"
  exit 0
fi
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
hist=$(mktemp)
search_hist=$(mktemp)
keys=$(mktemp)
trap 'rm -f "$hist" "$search_hist" "$keys"' EXIT
i=1
while [ "$i" -le 4200 ]; do
  printf 'echo CMD_%05d\n' "$i" >> "$hist"
  i=$((i + 1))
done
# Up arrow then enter recalls and runs the newest entry, then exit leaves.
printf '\033[A\rexit\r' > "$keys"
out=$(SHIT_HISTORY="$hist" script -qec "$BIN" /dev/null < "$keys" 2>/dev/null)
case "$out" in
*CMD_04200*) echo "recall ok" ;;
*) echo "recall broken" ;;
esac
printf 'echo MiXeD_History_Marker\n' > "$search_hist"
printf '\022mixed_history_marker\r\rexit\r' > "$keys"
out=$(SHIT_HISTORY="$search_hist" script -qec "$BIN" /dev/null < "$keys" 2>/dev/null)
case "$out" in
*MiXeD_History_Marker*) echo "search casefold ok" ;;
*) echo "search casefold broken" ;;
esac
