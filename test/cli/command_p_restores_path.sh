unset SHIT_FLAGS
# command -p resolves its operand against a default PATH, then the resolver
# must revert to the shell's own PATH. A bogus or a valid operand once left the
# resolver with no PATH, so every later command stopped being found.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf '#!/bin/sh\necho probe-ran\n' > "$dir/probecmd"
chmod +x "$dir/probecmd"

PATH="$dir:$PATH" "$BIN" -c 'command -p nonexistent_xyz 2>/dev/null; probecmd'
PATH="$dir:$PATH" "$BIN" -c 'command -p ls >/dev/null; probecmd'
echo "rc=$?"
