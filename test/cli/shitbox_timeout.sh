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
echo "rc=$?"

echo "--- timeout forces a kill after the grace period ---"
"$BIN" -c "shitbox timeout -k 0.02s 0.02s /bin/sh '$d/ignore.sh'" \
    >/dev/null 2>&1
echo "rc=$?"

echo "--- timeout zero allows completion ---"
"$BIN" -c 'shitbox timeout 0 /bin/sh -c "exit 9"'
echo "rc=$?"

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
