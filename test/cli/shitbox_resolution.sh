# The shitbox builtin prefix always works. In the default mood a bare coreutil
# name falls back to the shitbox utility when PATH has no binary of that name,
# while the sh mood reports a command not found. The --enable-shitbox flag and
# set -o shitbox enable the same fallback in every mood.
# An empty PATH isolates the resolution from the system coreutils.
unset SHIT_FLAGS

dir=$(mktemp -d) || exit 1
trap '[ -n "$dir" ] && /bin/rm -rf "$dir"' EXIT
printf '#!/bin/sh\nprintf "PATH seq\\n"\n' > "$dir/seq"
/bin/chmod +x "$dir/seq"

echo "=== shitbox prefix always works ==="
"$BIN" -c 'shitbox seq 3'

echo "=== default mood, empty PATH, falls back to shitbox ==="
"$BIN" -c 'PATH=; seq 3'
echo "rc=$?"

echo "=== sh mood, empty PATH, not found ==="
"$BIN" --mood sh -c 'PATH=; seq 3' 2>&1 | ./normalize-trace.sh "$BIN"; rc=${PIPESTATUS[0]}
echo "rc=$rc"

echo "=== set -o shitbox turns bare names on ==="
"$BIN" -c 'PATH=; set -o shitbox; seq 3'

echo "=== --enable-shitbox turns bare names on ==="
"$BIN" --enable-shitbox -c 'PATH=; seq 3'

echo "=== --enable-shitbox prefers a PATH binary ==="
PATH="$dir" "$BIN" --enable-shitbox -c 'seq 3'

echo "=== set -o shitbox prefers a PATH binary ==="
PATH="$dir" "$BIN" -c 'set -o shitbox; seq 3'

echo "=== --enable-shitbox works in the sh mood ==="
"$BIN" --mood sh --enable-shitbox -c 'PATH=; seq 3'

echo "=== command reports the enabled fallback ==="
"$BIN" --enable-shitbox -c 'PATH=; command -v seq; command -V seq'

echo "=== which reports the enabled fallback ==="
"$BIN" --enable-shitbox -c 'PATH=; which seq'

echo "=== a builtin name is not a shitbox utility ==="
"$BIN" -c 'shitbox echo routed via shitbox' 2>&1
echo "rc=$?"

echo "=== the shitbox name does not recurse ==="
"$BIN" -c 'shitbox shitbox' 2>&1
echo "rc=$?"

echo "=== unknown utility errors ==="
"$BIN" -c 'shitbox nope' 2>&1
echo "rc=$?"
