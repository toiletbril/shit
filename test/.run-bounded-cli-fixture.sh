#!/bin/bash

fixture=$1
timeout_seconds=${CLI_TEST_TIMEOUT_SECONDS:-60}
fixture_process=
fixture_session=
host_system=$(uname -s)
cleanup_is_armed=yes
pending_exit_status=

case $timeout_seconds in
''|*[!0-9]*)
    printf 'invalid CLI fixture timeout\n'
    exit 125
    ;;
esac

list_fixture_session_processes()
{
    if [ "$host_system" = Darwin ]; then
        process_ids=$(ps -Ao pid= 2>/dev/null) || return 1
        printf '%s\n' "$process_ids" | perl -e '
            $session = shift;
            exit 1 if syscall(310, 0) < 0;
            while (<STDIN>) {
                $process = int($_);
                $process_session = syscall(310, $process);
                print "$process\n" if $process_session == $session;
            }
        ' "$fixture_session"
        return
    fi

    if process_table=$(ps -Ao pid=,sess= 2>/dev/null); then
        :
    elif process_table=$(ps -Ao pid=,sid= 2>/dev/null); then
        :
    else
        return 1
    fi
    printf '%s\n' "$process_table" | while read -r process_id process_session; do
        if [ "$process_session" = "$fixture_session" ]; then
            printf '%s\n' "$process_id"
        fi
    done
}

terminate_fixture_tree()
{
    if [ "${OS-}" = Windows_NT ] &&
        command -v taskkill.exe >/dev/null 2>&1; then
        [ -n "$fixture_process" ] || return
        windows_process_id=$(ps -p "$fixture_process" -o winpid= 2>/dev/null |
            tr -d '[:space:]')
        if [ -n "$windows_process_id" ]; then
            taskkill.exe //PID "$windows_process_id" //T //F \
                >/dev/null 2>&1 || true
        fi
        kill -KILL "$fixture_process" 2>/dev/null || true
        return
    fi

    discovery_failed=no
    session_processes=$(list_fixture_session_processes) || {
        discovery_failed=yes
        session_processes=
    }
    if [ -n "$fixture_process$session_processes" ]; then
        kill -TERM $fixture_process $session_processes 2>/dev/null || true
    fi
    sleep 0.1
    session_processes=$(list_fixture_session_processes) || {
        discovery_failed=yes
        session_processes=
    }
    if [ -n "$fixture_process$session_processes" ]; then
        kill -KILL $fixture_process $session_processes 2>/dev/null || true
    fi
    [ "$discovery_failed" = no ]
}

cleanup_fixture_tree()
{
    if [ "$cleanup_is_armed" = yes ] && [ -n "$fixture_session" ]; then
        if ! terminate_fixture_tree; then
            printf 'fixture session discovery failed\n'
        fi
        if [ -n "$fixture_process" ]; then
            wait "$fixture_process" 2>/dev/null || true
        fi
    fi
}

request_exit()
{
    pending_exit_status=$1
    if [ -n "$fixture_session" ]; then
        exit "$pending_exit_status"
    fi
}

trap cleanup_fixture_tree EXIT
trap 'request_exit 130' INT
trap 'request_exit 143' TERM
trap 'request_exit 129' HUP

if [ "${OS-}" = Windows_NT ]; then
    BIN=$BIN SHIT_TEST_TIMEOUT_JOB_LIFETIME=leader \
        "$BIN" -p --mood sh -c \
        'shitbox timeout 0 /bin/sh -c '\''unset SHIT_TEST_TIMEOUT_JOB_LIFETIME; /bin/sh "$1"'\'' shell "$1"' \
        shell "$fixture" &
elif command -v setsid >/dev/null 2>&1; then
    BIN=$BIN setsid /bin/sh "$fixture" &
elif command -v perl >/dev/null 2>&1; then
    BIN=$BIN perl -MPOSIX -e 'POSIX::setsid(); exec @ARGV' \
        /bin/sh "$fixture" &
else
    printf 'cannot create a CLI fixture session\n'
    exit 125
fi
fixture_process=$!
fixture_session=$fixture_process
if [ -n "$pending_exit_status" ]; then
    exit "$pending_exit_status"
fi

attempt_count=0
attempt_limit=$((timeout_seconds * 10))
while kill -0 "$fixture_process" 2>/dev/null &&
    [ "$attempt_count" -lt "$attempt_limit" ]; do
    sleep 0.1
    attempt_count=$((attempt_count + 1))
done

if kill -0 "$fixture_process" 2>/dev/null; then
    printf 'fixture timed out\n'
    termination_status=0
    terminate_fixture_tree || termination_status=$?
    wait "$fixture_process" 2>/dev/null || true
    fixture_process=
    cleanup_is_armed=no
    if [ "$termination_status" -ne 0 ]; then
        printf 'fixture session discovery failed\n'
        exit 125
    fi
    exit 124
fi

wait "$fixture_process"
fixture_status=$?
fixture_process=
attempt_count=0
has_living_descendant=yes
session_discovery_failed=no
while [ "$has_living_descendant" = yes ] && [ "$attempt_count" -lt 1000 ]; do
    has_living_descendant=no
    if [ "${OS-}" != Windows_NT ]; then
        session_processes=$(list_fixture_session_processes) || {
            session_discovery_failed=yes
            break
        }
        for process_id in $session_processes; do
            if [ -n "$process_id" ]; then
                has_living_descendant=yes
                break
            fi
        done
    fi
    if [ "$has_living_descendant" = yes ]; then
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    fi
done
if [ "$session_discovery_failed" = yes ]; then
    printf 'fixture session discovery failed\n'
    terminate_fixture_tree 2>/dev/null || true
    cleanup_is_armed=no
    exit 125
fi
if [ "$has_living_descendant" = yes ]; then
    printf 'fixture leaked processes\n'
    terminate_fixture_tree
    cleanup_is_armed=no
    exit 125
fi

cleanup_is_armed=no
exit "$fixture_status"
