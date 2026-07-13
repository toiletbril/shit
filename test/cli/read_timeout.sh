#!/bin/sh

unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT

mkfifo "$d/query"
(
    exec 3> "$d/query"
    sleep 0.1
    printf y >&3
) &
writer=$!
"$BIN" -c "read -r -q -t 0.02 < '$d/query'; printf 'query=%s\n' \"\$?\""
wait "$writer" 2>/dev/null

mkfifo "$d/count"
(
    printf a
    sleep 0.1
    printf bc
) > "$d/count" &
writer=$!
"$BIN" -c "read -r -n 3 -t 0.02 value < '$d/count'; printf 'count=%s value=[%s]\n' \"\$?\" \"\$value\""
wait "$writer" 2>/dev/null

mkfifo "$d/continued"
(
    printf 'a\\\n'
    sleep 0.1
    printf 'b\n'
) > "$d/continued" &
writer=$!
"$BIN" -c "read -t 0.02 value < '$d/continued'; printf 'continued=%s value=[%s]\n' \"\$?\" \"\$value\"" 2>/dev/null
wait "$writer" 2>/dev/null

"$BIN" -c "read -r -t 0 -u 99 value </dev/null; printf 'invalid=%s\n' \"\$?\""

printf '' | "$BIN" -c "read -r -t 0.1 value; printf 'eof=%s value=[%s]\n' \"\$?\" \"\$value\""
