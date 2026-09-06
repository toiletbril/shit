#!/bin/sh
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This script verifies noninteractive history listing, maintenance, file
# access, multiline storage, and in-memory limit changes.

unset KOSH_FLAGS
dir=$(mktemp -d)
cleanup()
{
  if [ -n "$dir" ]; then
    "$TEST_SYSTEM_RM" -rf -- "$dir"
  fi
}
trap cleanup EXIT

printf 'echo one\nls\ncd /tmp\ngit status\n' > "$dir/hist"
export KOSH_HISTORY_FILE="$dir/hist"
echo "== the numbered list prints every entry =="
"$BIN" -c 'history'
echo "== a trailing count prints only the most recent entries =="
"$BIN" -c 'history 2'
echo "== a non-numeric count is rejected without printing the list =="
"$BIN" -c 'history foo; echo "rc=$?"'
echo "== a count past the list size still prints every entry, no overflow =="
"$BIN" -c 'history 999999999999999999999999'
echo "== the print flag echoes its operands and stores nothing =="
"$BIN" -c 'history -p alpha beta'
echo "== builtin history -a no longer reports an unknown builtin =="
"$BIN" -c 'builtin history -a; echo continued'
echo "== type reports the builtin =="
"$BIN" -c 'type history'
echo "== clear empties the list =="
"$BIN" -c 'history -c; history; echo cleared'

printf 'existing one\nexisting two\n' > "$dir/hist"
printf 'merged alpha\nmerged beta\n' > "$dir/extra"
echo "== history -r reads a named file into the list =="
"$BIN" -c 'history -r "$1"; history' history-test "$dir/extra"
printf 'named base' > "$dir/named-append"
echo "== history -a separates and appends the list to a named file =="
"$BIN" -c 'history -a "$1"; echo "rc=$?"' history-test "$dir/named-append"
cat "$dir/named-append"
echo "== history -a leaves its backing file unchanged =="
"$BIN" -c 'history -a "$KOSH_HISTORY_FILE"; echo "rc=$?"; history' history-test
echo "== a hard link aliases the backing file =="
"$BIN" -c 'koshkit ln "$1" "$2"; echo "rc=$?"' history-test "$dir/hist" \
  "$dir/history-alias"
echo "== history -a leaves an aliased backing file unchanged =="
"$BIN" -c 'history -a "$1"; echo "rc=$?"; history' history-test \
  "$dir/history-alias"
printf 'named base\n' > "$dir/named-write"
echo "== history -w replaces a named file with the list =="
"$BIN" -c 'history -w "$1"; echo "rc=$?"' history-test "$dir/named-write"
cat "$dir/named-write"
echo "== history -r on a missing file errors =="
"$BIN" -c 'history -r "$1"; echo "rc=$?"' history-test \
  "$dir/no-such-history" 2>/dev/null

printf '\001' > "$dir/invalid"
echo "== history -r rejects invalid data without changing the list =="
"$BIN" -c 'history -r "$1"; echo "rc=$?"; history' history-test \
  "$dir/invalid" 2>/dev/null

printf '\377\n' > "$dir/invalid-utf8"
echo "== history -r rejects malformed UTF-8 without changing the list =="
"$BIN" -c 'history -r "$1"; echo "rc=$?"; history' history-test \
  "$dir/invalid-utf8" 2>/dev/null

printf 'tail' > "$dir/unterminated"
printf 'next\n' > "$dir/next"
echo "== history -r separates an unterminated backing record =="
KOSH_HISTORY_FILE="$dir/unterminated" "$BIN" --no-init-files -c \
  'history -r "$1"; history' history-test "$dir/next"
printf 'next' > "$dir/next"
echo "== history -r terminates an unterminated imported record =="
KOSH_HISTORY_FILE="$dir/unterminated-import-backing" "$BIN" --no-init-files -c \
  'history -r "$1"; history' history-test "$dir/next"

: > "$dir/empty"
echo "== an empty import creates a missing backing file =="
KOSH_HISTORY_FILE="$dir/empty-backing" "$BIN" --no-init-files -c \
  'history -r "$1"; echo "rc=$?"' history-test "$dir/empty"

echo "== history stores into a missing backing file =="
KOSH_HISTORY_FILE="$dir/missing-backing" "$BIN" --no-init-files -c \
  'history -s created; echo "rc=$?"; history'
KOSH_HISTORY_FILE="$dir/missing-backing" "$BIN" --no-init-files -c 'history'

printf '\377\n' > "$dir/high-byte"
echo "== history accepts high bytes as file data =="
KOSH_HISTORY_FILE="$dir/high-byte" "$BIN" --no-init-files -c \
  'history | koshkit wc -l'

printf '\303\251\n\344\275\240\n\360\237\230\200\n' > "$dir/valid-utf8"
echo "== history accepts valid UTF-8 as imported file data =="
KOSH_HISTORY_FILE="$dir/valid-utf8-backing" "$BIN" --no-init-files -c \
  'history -r "$1"; echo "rc=$?"; history | koshkit wc -l' history-test \
  "$dir/valid-utf8"

for invalid_bytes in '\200' '\300\200' '\340\200\200' '\355\240\200' \
  '\364\220\200\200'; do
  printf "$invalid_bytes" > "$dir/invalid-utf8"
  printf '== history rejects malformed UTF-8 imported bytes %s ==\n' \
    "$invalid_bytes"
  "$BIN" -c 'history -r "$1"; echo "rc=$?"' history-test \
    "$dir/invalid-utf8" 2>/dev/null
done
printf '\303' > "$dir/invalid-utf8"
echo "== history rejects a truncated UTF-8 imported sequence =="
"$BIN" -c 'history -r "$1"; echo "rc=$?"' history-test \
  "$dir/invalid-utf8" 2>/dev/null

: > "$dir/oversized"
byte_count=0
while [ "$byte_count" -lt 4096 ]; do
  printf x >> "$dir/oversized"
  byte_count=$((byte_count + 1))
done
printf '\n' >> "$dir/oversized"
echo "== an oversized decoded record is reported =="
KOSH_HISTORY_FILE="$dir/oversized" "$BIN" --no-init-files -c 'history' 2>&1 | \
  grep -c "cannot read history at "
KOSH_HISTORY_FILE="$dir/oversized" "$BIN" --no-init-files -c \
  'history; echo "rc=$?"' 2>/dev/null

: > "$dir/concurrent"
echo "== concurrent history stores preserve both records =="
KOSH_HISTORY_FILE="$dir/concurrent" "$BIN" --no-init-files -c \
  'history -s first' &
first_pid=$!
KOSH_HISTORY_FILE="$dir/concurrent" "$BIN" --no-init-files -c \
  'history -s second' &
second_pid=$!
wait "$first_pid"
first_status=$?
wait "$second_pid"
second_status=$?
printf 'rc=%s,%s\n' "$first_status" "$second_status"
KOSH_HISTORY_FILE="$dir/concurrent" "$BIN" --no-init-files -c \
  'history | koshkit wc -l'

: > "$dir/duplicate"
echo "== consecutive duplicate stores succeed and retain one record =="
KOSH_HISTORY_FILE="$dir/duplicate" "$BIN" --no-init-files -c \
  'history -s repeated; first_status=$?; history -s repeated; \
second_status=$?; printf "rc=%s,%s\n" "$first_status" "$second_status"; history'

: > "$dir/zero-limit"
echo "== a zero KOSH_HISTORY_SIZE retains and stores nothing =="
KOSH_HISTORY_FILE="$dir/zero-limit" "$BIN" --no-init-files -c \
  'KOSH_HISTORY_SIZE=0; history -s x; single_status=$?; \
history -s repeated; first_status=$?; history -s repeated; second_status=$?; \
printf "rc=%s,%s,%s\n" "$single_status" "$first_status" "$second_status"; \
history; KOSH_HISTORY_SIZE=5; history; \
koshkit wc -l < "$KOSH_HISTORY_FILE"'

: > "$dir/repeated"
echo "== nonconsecutive duplicate stores remain separate records =="
KOSH_HISTORY_FILE="$dir/repeated" "$BIN" --no-init-files -c \
  'history -s alpha; history -s beta; history -s alpha; history'

printf 'first\n' > "$dir/equal-size"
printf second > "$dir/equal-size-replacement"
echo "== an equal size replacement refreshes append state =="
KOSH_HISTORY_FILE="$dir/equal-size" "$BIN" --no-init-files -c \
  'history >/dev/null; koshkit mv "$1" "$KOSH_HISTORY_FILE"; \
history -s third; history' history-test "$dir/equal-size-replacement"

printf 'mode one\n' > "$dir/mode-change"
echo "== a mode change on the backing file keeps the store readable =="
KOSH_HISTORY_FILE="$dir/mode-change" "$BIN" --no-init-files -c \
  'history >/dev/null; koshkit chmod 700 "$KOSH_HISTORY_FILE"; \
echo "rc=$?"; history -s modetwo; history'

: > "$dir/rewrite"
echo "== history deletion rewrites the no editor store =="
KOSH_HISTORY_FILE="$dir/rewrite" "$BIN" --no-init-files -c \
  'history -s first; history -s second; history -d 1; history'

printf '\377\nsecond\n' > "$dir/high-byte-rewrite"
echo "== history deletion preserves legacy high-byte records =="
KOSH_HISTORY_FILE="$dir/high-byte-rewrite" "$BIN" --no-init-files -c \
  'history -d 2; echo "rc=$?"; history | koshkit wc -l'

: > "$dir/rewrite-range"
echo "== a range deletion renumbers before the next store =="
KOSH_HISTORY_FILE="$dir/rewrite-range" "$BIN" --no-init-files -c \
  'history -s first; history -s second; history -s third; history -d 1-2; \
delete_status=$?; history -s fourth; store_status=$?; \
printf "rc=%s,%s\n" "$delete_status" "$store_status"; history'

printf 'valid\n' > "$dir/invalid-store"
echo "== invalid stored bytes leave existing history intact =="
KOSH_HISTORY_FILE="$dir/invalid-store" "$BIN" --no-init-files -c \
  "history -s \$'bad\\evalue'; printf 'rc=%s\\n' \"\$?\"; history" \
  2>/dev/null

for invalid_bytes in '\377' '\200' '\303' '\300\200' '\340\200\200' \
  '\355\240\200' '\364\220\200\200'; do
  printf 'valid\n' > "$dir/invalid-utf8-store"
  printf '== malformed UTF-8 stored bytes %s leave existing history intact ==\n' \
    "$invalid_bytes"
  KOSH_HISTORY_FILE="$dir/invalid-utf8-store" "$BIN" --no-init-files -c \
    "history -s \$'bad${invalid_bytes}value'; printf 'rc=%s\\n' \"\$?\"; history" \
    2>/dev/null
done

: > "$dir/synchronized"
echo "== running shells reload history written by another instance =="
KOSH_HISTORY_FILE="$dir/synchronized" "$BIN" --no-init-files -c \
  'history >/dev/null; printf ready > "$1"; attempt_count=0; \
while [ ! -e "$2" ] && [ "$attempt_count" -lt 500 ]; do \
koshkit sleep 0.01; attempt_count=$((attempt_count + 1)); done; \
[ -e "$2" ] || exit 1; history' \
  history-sync "$dir/sync-ready" "$dir/sync-go" > "$dir/sync-output" &
reader_pid=$!
attempt_count=0
while [ ! -e "$dir/sync-ready" ] && [ "$attempt_count" -lt 500 ]; do
  sleep 0.01
  attempt_count=$((attempt_count + 1))
done
if [ ! -e "$dir/sync-ready" ]; then
  : > "$dir/sync-go"
  wait "$reader_pid"
  echo "history synchronization reader timed out" >&2
  exit 1
fi
KOSH_HISTORY_FILE="$dir/synchronized" "$BIN" --no-init-files -c \
  'history -s shared-event'
: > "$dir/sync-go"
wait "$reader_pid"
cat "$dir/sync-output"

printf 'one\ntwo\nthree\nfour\nfive\n' > "$dir/limit"
echo "== KOSH_HISTORY_SIZE controls the visible retained window =="
KOSH_HISTORY_FILE="$dir/limit" "$BIN" --no-init-files -c \
  'KOSH_HISTORY_SIZE=2; history; KOSH_HISTORY_SIZE=5; history; history -s six; \
history'

printf 'good\n\001bad\n' > "$dir/damaged"
echo "== a bare listing reports a damaged backing file =="
KOSH_HISTORY_FILE="$dir/damaged" "$BIN" --no-init-files -c 'history' 2>&1 | \
  grep -c "cannot read history at .*: the file contains invalid data"
KOSH_HISTORY_FILE="$dir/damaged" "$BIN" --no-init-files -c 'history' 2>/dev/null
echo "rc=$?"

echo "== an operandless read names the resolved backing file =="
KOSH_HISTORY_FILE="$dir/no-such-backing" "$BIN" --no-init-files -c \
  'history -r' 2>&1 | grep -c "cannot read history at "
mkdir "$dir/backing-directory"
KOSH_HISTORY_FILE="$dir/backing-directory" "$BIN" --no-init-files -c \
  'history -r' 2>&1 | grep -c "cannot read history at "

: > "$dir/multiline"
echo "== a stored multiline event keeps its submitted shape =="
KOSH_HISTORY_FILE="$dir/multiline" "$BIN" --no-init-files -c \
  "history -s \$'line one\\nline two'; history"

printf 'named base\n' > "$dir/repeated-append"
echo "== a repeated named append stores only the events not written yet =="
KOSH_HISTORY_FILE="$dir/repeated-append-backing" "$BIN" --no-init-files -c \
  'history -s one; history -a "$1"; history -s two; history -a "$1"; \
echo "rc=$?"' history-test "$dir/repeated-append"
cat "$dir/repeated-append"

printf 'first target\n' > "$dir/append-one"
printf 'second target\n' > "$dir/append-two"
echo "== appends to different files each receive the retained list =="
KOSH_HISTORY_FILE="$dir/two-target-backing" "$BIN" --no-init-files -c \
  'history -s alpha; history -a "$1"; history -a "$2"; echo "rc=$?"' \
  history-test "$dir/append-one" "$dir/append-two"
cat "$dir/append-one"
cat "$dir/append-two"

echo "== a named write to a directory fails and leaves the directory in place =="
KOSH_HISTORY_FILE="$dir/write-fail-backing" "$BIN" --no-init-files -c \
  'history -s kept; history -w "$1"; echo "rc=$?"; \
if [ -d "$1" ]; then echo "directory intact"; fi; history' history-test \
  "$dir/backing-directory" 2>/dev/null
KOSH_HISTORY_FILE="$dir/write-fail-backing" "$BIN" --no-init-files -c \
  'history -w "$1"' history-test "$dir/backing-directory" 2>&1 | \
  grep -c "cannot write history to '"

echo "== a named append to a directory fails =="
KOSH_HISTORY_FILE="$dir/append-fail-backing" "$BIN" --no-init-files -c \
  'history -s kept; history -a "$1"; echo "rc=$?"' history-test \
  "$dir/backing-directory" 2>/dev/null

echo "== an operandless write names the resolved backing file =="
KOSH_HISTORY_FILE="$dir/backing-directory" "$BIN" --no-init-files -c \
  'history -w; echo "rc=$?"' 2>&1 | grep -c "cannot write history at "

printf 'append base\n' > "$dir/append-limit-target"
echo "== a named append is bounded by KOSH_HISTORY_SIZE =="
KOSH_HISTORY_FILE="$dir/append-limit" "$BIN" --no-init-files -c \
  'KOSH_HISTORY_SIZE=2; history -s one; history -s two; history -s three; \
history -a "$1"; echo "rc=$?"' history-test "$dir/append-limit-target"
cat "$dir/append-limit-target"

printf 'import one\nimport two\n' > "$dir/import-source"
echo "== a repeated new read skips the bytes it already took =="
KOSH_HISTORY_FILE="$dir/import-new-backing" "$BIN" --no-init-files -c \
  'history -n "$1"; history -n "$1"; history' history-test "$dir/import-source"
echo "== a repeated read takes the whole file again =="
KOSH_HISTORY_FILE="$dir/import-read-backing" "$BIN" --no-init-files -c \
  'history -r "$1"; history -r "$1"; history' history-test "$dir/import-source"

printf 'one\ntwo\nthree\nfour\nfive\n' > "$dir/write-trim"
echo "== an operandless write trims the backing file to the retained window =="
KOSH_HISTORY_FILE="$dir/write-trim" "$BIN" --no-init-files -c \
  'KOSH_HISTORY_SIZE=2; history -w; echo "rc=$?"'
cat "$dir/write-trim"

printf 'one\ntwo\nthree\nfour\nfive\n' > "$dir/write-limit"
echo "== a named write is bounded by KOSH_HISTORY_SIZE =="
KOSH_HISTORY_FILE="$dir/write-limit" "$BIN" --no-init-files -c \
  'KOSH_HISTORY_SIZE=2; history -w "$1"; echo "rc=$?"' history-test \
  "$dir/write-limit-target"
cat "$dir/write-limit-target"

: > "$dir/carriage-return"
echo "== a stored carriage return stays inside the event =="
KOSH_HISTORY_FILE="$dir/carriage-return" "$BIN" --no-init-files -c \
  "history -s \$'a\\rb'; printf 'rc=%s\\n' \"\$?\"; listed=\$(history); \
printf '%s\\n' \"\${listed//\$'\\r'/R}\""

printf 'crlf one\r\ncrlf two\r\n' > "$dir/crlf"
echo "== a CRLF backing file drops only the record terminator =="
KOSH_HISTORY_FILE="$dir/crlf" "$BIN" --no-init-files -c \
  "listed=\$(history); printf '%s\\n' \"\${listed//\$'\\r'/R}\""

printf 'race one\nrace two\nrace three\n' > "$dir/race"
echo "== a concurrent deletion and store leave a readable store =="
KOSH_HISTORY_FILE="$dir/race" "$BIN" --no-init-files -c 'history -d 2' &
delete_pid=$!
KOSH_HISTORY_FILE="$dir/race" "$BIN" --no-init-files -c 'history -s racefour' &
store_pid=$!
wait "$delete_pid"
delete_status=$?
wait "$store_pid"
store_status=$?
printf 'rc=%s,%s\n' "$delete_status" "$store_status"
KOSH_HISTORY_FILE="$dir/race" "$BIN" --no-init-files -c \
  'history >/dev/null; echo "rc=$?"'

printf 'grow one\n' > "$dir/grow"
echo "== a store after an external append keeps both records =="
KOSH_HISTORY_FILE="$dir/grow" "$BIN" --no-init-files -c \
  'history >/dev/null; printf "grow two\n" >> "$KOSH_HISTORY_FILE"; \
history -s growthree; history'
