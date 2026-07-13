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
