unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
# An open of a named pipe blocks until its peer arrives. An untrapped interrupt
# ends the open, and the shell reports the interrupt against the redirection
# that owns it. An ignored interrupt leaves the open blocked, and the file is
# still opened once the peer arrives.
directory=$(mktemp -d)
trap '[ -n "$directory" ] && /bin/rm -rf "$directory"' EXIT
mkfifo "$directory/read" "$directory/write" "$directory/exec" "$directory/kept" \
  "$directory/wc" "$directory/cksum" "$directory/grep"
printf 'first\n' > "$directory/first"

echo "== an untrapped interrupt ends a blocking read open:"
"$BIN" --no-traces --mood bash -c 'fifo=$1
( /bin/sleep 1; kill -INT $$ ) &
read -r line < "$fifo"
echo "unreached=[$line]"
wait' shell "$directory/read" 2>&1
echo "rc=$?"

echo "== an untrapped interrupt ends a blocking write open:"
"$BIN" --no-traces --mood bash -c 'fifo=$1
( /bin/sleep 1; kill -INT $$ ) &
echo payload > "$fifo"
echo unreached
wait' shell "$directory/write" 2>&1
echo "rc=$?"

echo "== an exec redirection reports the interrupt once:"
"$BIN" --no-traces --mood bash -c 'fifo=$1
( /bin/sleep 1; kill -INT $$ ) &
exec 9< "$fifo"
echo unreached
wait' shell "$directory/exec" 2>&1
echo "rc=$?"

echo "== wc buffers completed rows before an interrupted source:"
"$BIN" --no-traces --mood bash -c 'directory=$1
fifo=$2
cd "$directory"
( exec 9> "$fifo"; /bin/sleep 2 ) &
( /bin/sleep 1; kill -INT $$ ) &
koshkit wc -c first "$fifo"
echo "wc-status=$?"
wait' shell "$directory" "$directory/wc" 2>&1
echo "rc=$?"

echo "== cksum prints completed rows before an interrupted source:"
"$BIN" --no-traces --mood bash -c 'directory=$1
fifo=$2
cd "$directory"
( exec 9> "$fifo"; /bin/sleep 2 ) &
( /bin/sleep 1; kill -INT $$ ) &
koshkit cksum first "$fifo"
echo "cksum-status=$?"
wait' shell "$directory" "$directory/cksum" 2>&1
echo "rc=$?"

echo "== grep reads an earlier regular file before opening a later pipe:"
"$BIN" -c 'koshkit seq 40000' > "$directory/grep-first"
(cd "$directory" && exec "$BIN" --no-traces --mood bash -c \
  'koshkit grep . grep-first grep') > "$directory/grep-output" 2>/dev/null &
grep_pid=$!
grep_output_is_ready=no
grep_poll_count=0
while [ "$grep_poll_count" -lt 500 ]; do
  if [ -s "$directory/grep-output" ]; then
    grep_output_is_ready=yes
    break
  fi
  if ! kill -0 "$grep_pid" 2>/dev/null; then break; fi
  /bin/sleep 0.01
  grep_poll_count=$((grep_poll_count + 1))
done
kill -TERM "$grep_pid" 2>/dev/null || :
wait "$grep_pid" 2>/dev/null
grep_status=$?
echo "grep-status=$grep_status"
if [ "$grep_output_is_ready" = yes ]; then
  echo "grep-first=printed"
else
  echo "grep-first=missing"
fi

echo "== an ignored interrupt leaves the open blocked until its peer arrives:"
"$BIN" --no-traces --mood bash -c 'fifo=$1
trap "" INT
( /bin/sleep 1; kill -INT $$ ) &
( /bin/sleep 2; echo payload > "$fifo" ) &
read -r line < "$fifo"
echo "kept=[$line] status=$?"
wait' shell "$directory/kept" 2>&1
echo "rc=$?"
