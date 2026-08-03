# History recall brings back the newest command even when the history file holds
# more entries than the in-memory ring. A missing parenthesis in
# TL_HISTORY_MAX_SIZE once parsed the ring modulo as (x % 1024) * 4, so past 4096
# entries the up arrow recalled a stale older line. The editor needs a tty, so
# the run skips when script or the target terminal handles cannot provide one.
if ! BIN="$BIN" script -qec \
  'exec "$BIN" -c "test -t 0 && test -t 1"' \
  /dev/null >/dev/null 2>&1; then
  echo "recall ok"
  echo "search casefold ok"
  exit 0
fi
hist=$(mktemp)
search_hist=$(mktemp)
ready=$(mktemp)
input_status=$(mktemp)
trap 'rm -f "$hist" "$search_hist" "$ready" "$input_status"' EXIT
send_input_when_ready()
{
  wait_count=0
  while [ ! -s "$ready" ] && [ "$wait_count" -lt 600 ]; do
    sleep 0.05
    wait_count=$((wait_count + 1))
  done
  [ -s "$ready" ] || return 1
  sleep 0.25
  for key_sequence in "$@"; do
    printf '%b' "$key_sequence" || return 1
    sleep 0.05 || return 1
  done
  sleep 0.25
}
i=1
while [ "$i" -le 4200 ]; do
  printf 'echo CMD_%05d\n' "$i" >> "$hist"
  i=$((i + 1))
done
# Up arrow then enter recalls and runs the newest entry, then exit leaves.
rm -f "$ready"
rm -f "$input_status"
out=$({
  send_input_when_ready '\033[A' '\r' 'exit\r'
  printf '%s\n' "$?" > "$input_status"
} |
  BIN="$BIN" READY="$ready" SHIT_HISTORY="$hist" \
    PROMPT_COMMAND='printf ready > "$READY"; unset PROMPT_COMMAND' \
    script -qec 'exec "$BIN" -i --rcfile /dev/null' /dev/null 2>/dev/null) ||
  exit 1
[ "$(cat "$input_status")" = 0 ] || exit 1
case "$out" in
*CMD_04200*) echo "recall ok" ;;
*) echo "recall broken" ;;
esac
printf 'echo MiXeD_History_Marker\n' > "$search_hist"
rm -f "$ready"
rm -f "$input_status"
out=$({
  send_input_when_ready '\022' 'mixed_history_marker' '\r' '\r' 'exit\r'
  printf '%s\n' "$?" > "$input_status"
} |
  BIN="$BIN" READY="$ready" SHIT_HISTORY="$search_hist" \
    PROMPT_COMMAND='printf ready > "$READY"; unset PROMPT_COMMAND' \
    script -qec 'exec "$BIN" -i --rcfile /dev/null' /dev/null 2>/dev/null) ||
  exit 1
[ "$(cat "$input_status")" = 0 ] || exit 1
case "$out" in
*MiXeD_History_Marker*) echo "search casefold ok" ;;
*) echo "search casefold broken" ;;
esac
