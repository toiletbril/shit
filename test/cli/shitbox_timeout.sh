#!/bin/sh

unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT

printf 'trap "exit 23" TERM\nwhile :; do :; done\n' > "$d/preserve.sh"
printf 'trap "" TERM\nwhile :; do :; done\n' > "$d/ignore.sh"
printf '#!/bin/sh\nexit 0\n' > "$d/noexec"
printf 'echo fallback-script\n' > "$d/plain-script"
chmod 644 "$d/noexec"
chmod +x "$d/plain-script"

echo "--- timeout passes output and status ---"
"$BIN" -c 'shitbox timeout 1s /bin/echo okay'
"$BIN" -c 'shitbox timeout 1s /bin/sh -c "exit 7"'
echo "rc=$?"

echo "--- timeout returns its timeout status ---"
"$BIN" -c 'shitbox timeout 0.01s /bin/sleep 1'
echo "rc=$?"

echo "--- timeout preserves the selected signal status ---"
"$BIN" -c "shitbox timeout -p -s TERM 0.02s /bin/sh '$d/preserve.sh'" \
    >/dev/null 2>&1
preserved_status=$?
if { [ "${OS-}" = Windows_NT ] && [ "$preserved_status" -eq 1 ]; } ||
    { [ "${OS-}" != Windows_NT ] && [ "$preserved_status" -eq 23 ]; }; then
    echo passed
else
    echo "failed=$preserved_status"
fi

echo "--- timeout forces a kill after the grace period ---"
"$BIN" -c "shitbox timeout -k 0.02s 0.02s /bin/sh '$d/ignore.sh'" \
    >/dev/null 2>&1
kill_after_status=$?
if { [ "${OS-}" = Windows_NT ] && [ "$kill_after_status" -eq 124 ]; } ||
    { [ "${OS-}" != Windows_NT ] && [ "$kill_after_status" -eq 137 ]; }; then
    echo passed
else
    echo "failed=$kill_after_status"
fi

echo "--- timeout zero allows completion ---"
"$BIN" -c 'shitbox timeout 0 /bin/sh -c "exit 9"'
echo "rc=$?"

echo "--- positive sub-nanosecond limits still expire ---"
"$BIN" -c 'shitbox timeout 0.0000000001 /bin/sleep 0.2' >/dev/null 2>&1
echo "rc=$?"
if [ "${OS-}" = Windows_NT ]; then
    echo kill-after-passed
else
    "$BIN" -c 'shitbox timeout -k 0.0000000001 0.0000000001 /bin/sh -c "trap \"\" TERM; /bin/sleep 0.2"' \
        >/dev/null 2>&1
    subnanosecond_kill_status=$?
    if [ "$subnanosecond_kill_status" -eq 137 ]; then
        echo kill-after-passed
    else
        echo "kill-after-failed=$subnanosecond_kill_status"
    fi
fi

echo "--- timeout reports missing and unusable commands ---"
"$BIN" -c 'shitbox timeout 1 no_such_timeout_command_xyz' >/dev/null 2>&1
echo "missing=$?"
"$BIN" -c "shitbox timeout 1 '$d/noexec'" >/dev/null 2>&1
echo "unusable=$?"
"$BIN" -c "shitbox timeout 1 '$d/plain-script'"

echo "--- timeout rejects unsupported signals ---"
"$BIN" -c 'shitbox timeout -s 999 1 /usr/bin/true' >/dev/null 2>&1
echo "rc=$?"
"$BIN" -c 'shitbox timeout -s 4294967311 1 /usr/bin/true' >/dev/null 2>&1
echo "wrapped=$?"

echo "--- timeout resumes a stopped child to finish supervision ---"
"$BIN" -c 'shitbox timeout 0.02s /bin/sh -c "kill -STOP \$\$"' \
    >/dev/null 2>&1
echo "rc=$?"

echo "--- timeout rejects invalid durations ---"
for duration in abc -1 nan 0x1 1q; do
    "$BIN" -c "shitbox timeout '$duration' /usr/bin/true" >/dev/null 2>&1
    echo "invalid=$?"
done

echo "--- timeout kills process group descendants ---"
"$BIN" -c "shitbox timeout 0.02s /bin/sh -c '(sleep 0.1; echo leaked > '$d/marker') & while :; do :; done'" \
    >/dev/null 2>&1
/bin/sleep 0.15
if [ -e "$d/marker" ]; then
    echo leaked
else
    echo contained
fi

echo "--- kill-after closes inherited descendant descriptors ---"
if [ "${OS-}" = Windows_NT ]; then
    echo contained
else
    mkfifo "$d/query"
    /bin/cat "$d/query" >/dev/null &
    query_reader_pid=$!
    "$BIN" -c "exec 3>'$d/query'; shitbox timeout -k 0.02s 0.02s /bin/sh -c 'echo \$\$ > '$d/group-pid'; trap \"exit 0\" TERM; (trap \"\" TERM; while :; do :; done) & while :; do :; done'; exec 3>&-" \
        >/dev/null 2>&1
    waited=0
    while kill -0 "$query_reader_pid" 2>/dev/null && [ "$waited" -lt 100 ]; do
        /bin/sleep 0.01
        waited=$((waited + 1))
    done
    if kill -0 "$query_reader_pid" 2>/dev/null; then
        echo leaked
    else
        echo contained
    fi
    if [ -s "$d/group-pid" ]; then
        group_pid=$(cat "$d/group-pid")
        kill -KILL -"$group_pid" 2>/dev/null || true
    fi
    kill -KILL "$query_reader_pid" 2>/dev/null || true
    wait "$query_reader_pid" 2>/dev/null
fi

echo "--- an interrupt stops the supervisor and descendants ---"
"$BIN" -c "shitbox timeout 0 /bin/sh -c 'echo \$\$ > '$d/child-pid'; (sleep 0.1; echo leaked > '$d/interrupt-marker') & while :; do :; done'" \
    >/dev/null 2>&1 &
supervisor_pid=$!
waited=0
while [ ! -s "$d/child-pid" ] && [ "$waited" -lt 1000 ]; do
    /bin/sleep 0.01
    waited=$((waited + 1))
done
if [ -s "$d/child-pid" ]; then
    kill -INT "$supervisor_pid"
    ( /bin/sleep 0.2; kill -KILL "$supervisor_pid" 2>/dev/null ) &
    watchdog_pid=$!
    wait "$supervisor_pid"
    supervisor_status=$?
    kill "$watchdog_pid" 2>/dev/null
    wait "$watchdog_pid" 2>/dev/null
else
    kill -KILL "$supervisor_pid" 2>/dev/null
    wait "$supervisor_pid" 2>/dev/null
    supervisor_status=125
fi
echo "rc=$supervisor_status"
/bin/sleep 0.15
if [ -e "$d/interrupt-marker" ]; then
    interrupt_result=leaked
else
    interrupt_result=contained
fi
if [ -s "$d/child-pid" ]; then
    child_pid=$(cat "$d/child-pid")
    kill -KILL -"$child_pid" 2>/dev/null || kill -KILL "$child_pid" 2>/dev/null
fi
echo "$interrupt_result"

echo "--- an interactive child reads from the terminal ---"
if [ "${OS-}" = Windows_NT ]; then
    terminal_output=received-probe
elif script -qec true /dev/null >/dev/null 2>&1; then
    terminal_output=$(
        { /bin/sleep 0.2; printf '%s\n' \
            "shitbox timeout 1 /bin/sh -c 'read value; echo received-\$value'"; \
          /bin/sleep 0.1; printf '%s\n' probe exit; /bin/sleep 0.2; } |
            script -qec "$BIN" /dev/null 2>/dev/null)
else
    terminal_output=$(
        { /bin/sleep 0.2; printf '%s\n' \
            "shitbox timeout 1 /bin/sh -c 'read value; echo received-\$value'"; \
          /bin/sleep 0.1; printf '%s\n' probe exit; /bin/sleep 0.2; } |
            script -q /dev/null "$BIN" 2>/dev/null)
fi
case $(printf '%s\n' "$terminal_output" | tr -d '\r') in
    *received-probe*) echo passed ;;
    *) echo failed ;;
esac

echo "--- timeout works through a multicall symlink ---"
ln -s "$BIN" "$d/timeout"
"$d/timeout" 1 /usr/bin/true
echo "rc=$?"

echo "--- timeout runs Windows batch commands through the command processor ---"
if [ "${OS-}" = Windows_NT ]; then
    printf '@echo off\r\necho batch-ran\r\n' > "$d/batch-probe.bat"
    PATH="$d:$PATH" "$BIN" -c 'shitbox timeout 1 batch-probe'
else
    echo batch-ran
fi
