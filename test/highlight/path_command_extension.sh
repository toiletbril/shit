dir=$(mktemp -d)
trap '/bin/rm -rf "$dir"' EXIT
if [ "${OS-}" = Windows_NT ]; then
    executable_name=explicit-probe.exe
else
    executable_name=EXPLICIT-PROBE.EXE
fi
printf '#!/bin/sh\n' > "$dir/$executable_name"
chmod +x "$dir/$executable_name"
PATH="$dir" "$BIN" --debug-highlight-at 'EXPLICIT-PROBE.EXE'
PATH="$dir" "$BIN" --debug-highlight-at 'EXPLICIT-P'
printf '#!/bin/sh\n' > "$dir/collision.com"
chmod +x "$dir/collision.com"
PATH="$dir" "$BIN" --debug-highlight-at 'collision.exe'
mkdir "$dir/directory.exe"
PATH="$dir" "$BIN" --debug-highlight-at 'directory.exe'
mkdir "$dir/blocker-first" "$dir/blocker-second"
mkdir "$dir/blocker-first/blocked.exe"
printf '#!/bin/sh\n' > "$dir/blocker-second/blocked.exe"
chmod +x "$dir/blocker-second/blocked.exe"
PATH="$dir/blocker-first:$dir/blocker-second" \
    "$BIN" --debug-highlight-at 'blocked.exe'
: > "$dir/foo"
printf '#!/bin/sh\n' > "$dir/foobar"
chmod +x "$dir/foobar"
PATH="$dir" "$BIN" --debug-highlight-at 'foo'
