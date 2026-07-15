#!/bin/sh

unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT

(
    sleep 1
    printf y
) | "$BIN" -c "read -r -q -t 0.02; printf 'query=%s\n' \"\$?\""

(
    printf a
    sleep 0.1
    printf bc
) | "$BIN" -c "read -r -n 3 -t 0.02 value; printf 'count=%s value=[%s]\n' \"\$?\" \"\$value\""

(
    printf 'a\\\n'
    sleep 0.1
    printf 'b\n'
) | "$BIN" -c "read -t 0.02 value; printf 'continued=%s value=[%s]\n' \"\$?\" \"\$value\"" 2>/dev/null

"$BIN" -c "read -r -t 0 -u 99 value </dev/null; printf 'invalid=%s\n' \"\$?\""

printf '' | "$BIN" -c "read -r -t 0.1 value; printf 'eof=%s value=[%s]\n' \"\$?\" \"\$value\""
