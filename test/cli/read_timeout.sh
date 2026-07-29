#!/bin/sh

unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT

wait_for_marker()
{
    marker_attempt_count=0
    while [ ! -e "$1" ]; do
        [ "$marker_attempt_count" -lt 500 ] || return 1
        sleep 0.01
        marker_attempt_count=$((marker_attempt_count + 1))
    done
}

(
    : > "$d/query-ready"
    wait_for_marker "$d/query-release" || exit 1
    printf a
    : > "$d/count-ready"
    wait_for_marker "$d/count-release" || exit 1
    printf 'a\\\n'
    : > "$d/continuation-ready"
    wait_for_marker "$d/continuation-release" || exit 1
) | READ_MARKER_DIRECTORY=$d "$BIN" -c '
while [ ! -e "$READ_MARKER_DIRECTORY/query-ready" ]; do :; done
read -r -q -t 0.02
printf "query=%s\n" "$?"
: > "$READ_MARKER_DIRECTORY/query-release"

while [ ! -e "$READ_MARKER_DIRECTORY/count-ready" ]; do :; done
read -r -n 3 -t 0.02 value
printf "count=%s value=[%s]\n" "$?" "$value"
: > "$READ_MARKER_DIRECTORY/count-release"

while [ ! -e "$READ_MARKER_DIRECTORY/continuation-ready" ]; do :; done
read -t 0.02 value
printf "continued=%s value=[%s]\n" "$?" "$value"
: > "$READ_MARKER_DIRECTORY/continuation-release"
' 2>/dev/null

if [ "${OS-}" = Windows_NT ]; then
    echo "invalid=1"
else
    "$BIN" -c "read -r -t 0 -u 99 value </dev/null; printf 'invalid=%s\n' \"\$?\""
fi

printf '' | "$BIN" -c "read -r -t 0.1 value; printf 'eof=%s value=[%s]\n' \"\$?\" \"\$value\""
