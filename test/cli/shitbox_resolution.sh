# The shitbox builtin prefix always works. In the default mood a bare coreutil
# name falls back to the shitbox utility when PATH has no binary of that name,
# while the sh mood reports a command not found. The --enable-shitbox flag and
# set -o shitbox turn on the applet mode where a bare name beats a PATH binary.
# An empty PATH isolates the resolution from the system coreutils.
unset SHIT_FLAGS

echo "=== shitbox prefix always works ==="
"$BIN" -c 'shitbox seq 3'

echo "=== default mood, empty PATH, falls back to shitbox ==="
"$BIN" -c 'PATH=; seq 3'
echo "rc=$?"

echo "=== sh mood, empty PATH, not found ==="
"$BIN" --mood sh -c 'PATH=; seq 3' 2>&1
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
