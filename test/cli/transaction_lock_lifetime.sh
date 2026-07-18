directory=$(mktemp -d)
trap 'test -n "$directory" && /bin/rm -rf "$directory"' EXIT

"$BIN" -p --mood sh -c '
    shitbox flock "$1" /bin/sh -c "printf held > \"\$1\"" shell "$2"
' shell "$directory" "$directory/normal"
printf 'normal=%s help=%s list=%s\n' "$(cat "$directory/normal")" \
    "$("$BIN" -c 'shitbox flock --help' | grep -c transaction-held-lock)" \
    "$("$BIN" -c 'shitbox --list' | grep -c '^flock$')"

LOCK_DIRECTORY=$directory STATE_DIRECTORY=$directory SHELL_BINARY=$BIN \
    "$BIN" -p --mood sh -c '
    shitbox flock --transaction-held-lock "$LOCK_DIRECTORY" "$SHELL_BINARY" \
        -p --mood sh -c '\''shitbox touch "$STATE_DIRECTORY/started"
        shitbox sleep 0.5
        shitbox touch "$STATE_DIRECTORY/finished"'\''
' &
wrapper_process=$!
attempt_count=0
while [ ! -e "$directory/started" ] && [ "$attempt_count" -lt 200 ]; do
    sleep 0.01
    attempt_count=$((attempt_count + 1))
done
test -e "$directory/started"
kill -9 "$wrapper_process"
wait "$wrapper_process" 2>/dev/null || :

LOCK_DIRECTORY=$directory STATE_DIRECTORY=$directory SHELL_BINARY=$BIN \
    "$BIN" -p --mood sh -c '
    shitbox flock --transaction-held-lock "$LOCK_DIRECTORY" "$SHELL_BINARY" \
        -p --mood sh -c '\''shitbox touch "$STATE_DIRECTORY/acquired"'\''
' &
second_process=$!
sleep 0.1
if [ ! -e "$directory/acquired" ]; then
    echo 'transaction child retains the process lock'
fi
wait "$second_process"
if [ -e "$directory/finished" ] && [ -e "$directory/acquired" ]; then
    echo 'the next transaction acquires the released lock'
fi
