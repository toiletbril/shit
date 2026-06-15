# The shitbox builtin is always present, while the bare utility names resolve as
# commands only under the --enable-shitbox flag or set -o shitbox. An empty PATH
# isolates the resolution from the system coreutils, so the off case is a clean
# command-not-found rather than a system binary.
unset SHIT_FLAGS

echo "=== shitbox prefix always works ==="
"$BIN" -c 'shitbox seq 3'

echo "=== bare name off, empty PATH, not found ==="
"$BIN" -c 'PATH=; seq 3' 2>&1
echo "rc=$?"

echo "=== set -o shitbox turns bare names on ==="
"$BIN" -c 'PATH=; set -o shitbox; seq 3'

echo "=== --enable-shitbox turns bare names on ==="
"$BIN" --enable-shitbox -c 'PATH=; seq 3'

echo "=== an existing builtin name routes to the builtin ==="
"$BIN" -c 'shitbox echo routed via shitbox'

echo "=== unknown utility errors ==="
"$BIN" -c 'shitbox nope' 2>&1
echo "rc=$?"
