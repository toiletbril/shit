directory=$(mktemp -d) || exit 1
wrapper_process=
acquisition_process=
transaction_state=not_started
flock_command=$(command -v flock 2>/dev/null || :)
if [ -n "$flock_command" ] && \
    ! "$flock_command" --help 2>&1 | grep -q conflict-exit-code; then
    flock_command=
fi

wait_for_path() {
    path=$1
    attempt_count=0
    while [ ! -e "$path" ] && [ "$attempt_count" -lt 3000 ]; do
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    done
    test -e "$path"
}

probe_directory_lock() {
    if [ -n "$flock_command" ]; then
        "$flock_command" -E 75 -n "$directory" true
        probe_status=$?
        case $probe_status in
        0) return 0 ;;
        75) return 1 ;;
        *) return 2 ;;
        esac
    fi

    perl -MFcntl=:flock -MErrno=EAGAIN,EWOULDBLOCK -e '
        open my $lock, "<", $ARGV[0] or exit 2;
        exit 0 if flock($lock, LOCK_EX | LOCK_NB);
        exit(($! == EAGAIN || $! == EWOULDBLOCK) ? 1 : 2);
    ' "$directory"
}

wait_for_lock_release() {
    attempt_count=0
    while [ "$attempt_count" -lt 3000 ]; do
        probe_directory_lock
        probe_status=$?
        case $probe_status in
        0) return 0 ;;
        1) sleep 0.01 ;;
        *) return 1 ;;
        esac
        attempt_count=$((attempt_count + 1))
    done
    return 1
}

wait_for_acquisition_result() {
    attempt_count=0
    while [ ! -e "$directory/acquisition-finished" ] && \
        [ ! -e "$directory/acquisition-failed" ] && \
        [ "$attempt_count" -lt 3000 ]; do
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    done
    test -e "$directory/acquisition-finished" || \
        test -e "$directory/acquisition-failed"
}

cleanup() {
    if [ -z "$directory" ] || [ ! -d "$directory" ]; then
        return
    fi

    : > "$directory/release"
    if [ -n "$wrapper_process" ]; then
        kill -9 "$wrapper_process" 2>/dev/null || :
        wait "$wrapper_process" 2>/dev/null || :
    fi
    if [ -n "$acquisition_process" ]; then
        kill -9 "$acquisition_process" 2>/dev/null || :
        wait "$acquisition_process" 2>/dev/null || :
    fi
    if [ "$transaction_state" = launched ]; then
        wait_for_path "$directory/started" 2>/dev/null || :
    fi
    if [ -e "$directory/started" ]; then
        wait_for_lock_release 2>/dev/null || :
    fi
    /bin/rm -rf "$directory"
}

trap cleanup EXIT

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
        attempt_count=0
        while [ ! -e "$STATE_DIRECTORY/release" ] && [ "$attempt_count" -lt 3000 ]; do
            shitbox sleep 0.01
            attempt_count=$((attempt_count + 1))
        done
        test -e "$STATE_DIRECTORY/release" || exit 1
        shitbox touch "$STATE_DIRECTORY/finished"'\''
' &
wrapper_process=$!
transaction_state=launched
wait_for_path "$directory/started" || exit 1
transaction_state=started
kill -9 "$wrapper_process"
wait "$wrapper_process" 2>/dev/null || :
wrapper_process=

probe_directory_lock
probe_status=$?
if [ "$probe_status" -eq 1 ]; then
    echo 'transaction child retains the process lock'
else
    exit 1
fi

: > "$directory/release"
wait_for_path "$directory/finished" || exit 1
wait_for_lock_release || exit 1
(
    if LOCK_DIRECTORY=$directory STATE_DIRECTORY=$directory SHELL_BINARY=$BIN \
        "$BIN" -p --mood sh -c '
        shitbox flock "$LOCK_DIRECTORY" "$SHELL_BINARY" -p --mood sh -c \
            '\''shitbox touch "$STATE_DIRECTORY/acquired"'\''
    '; then
        : > "$directory/acquisition-finished"
    else
        : > "$directory/acquisition-failed"
        exit 1
    fi
) &
acquisition_process=$!
wait_for_acquisition_result || exit 1
wait "$acquisition_process" || exit 1
acquisition_process=
test ! -e "$directory/acquisition-failed" || exit 1
test -e "$directory/acquired" || exit 1
transaction_state=complete
echo 'the next transaction acquires the released lock'
