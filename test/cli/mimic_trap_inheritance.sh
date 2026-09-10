unset KOSH_FLAGS
# The mood a shebang selects under mimicry decides which trap conditions the
# script may name. The sh mood rejects DEBUG, ERR, and RETURN, and the bash mood
# accepts all three. The same mood reaches a script operand and a script run as
# a command. An explicit --mood overrides the shebang in both directions.
directory=$(mktemp -d)
trap '[ -n "$directory" ] && /bin/rm -rf "$directory"' EXIT

printf '#!/bin/sh\ntrap "echo action" ERR\necho "err-rc=$?"\ntrap "echo action" RETURN\necho "return-rc=$?"\ntrap "echo action" DEBUG\necho "debug-rc=$?"\necho body\n' \
    > "$directory/sh-script"
printf '#!/bin/bash\ntrap "echo action" ERR\necho "err-rc=$?"\ntrap "echo action" RETURN\necho "return-rc=$?"\ntrap "echo action" DEBUG\necho "debug-rc=$?"\ntrap - DEBUG\necho body\n' \
    > "$directory/bash-script"
/bin/chmod +x "$directory/sh-script" "$directory/bash-script"

echo "== an sh shebang rejects the three bash conditions:"
"$BIN" --no-traces -I "$directory/sh-script" > "$directory/out" 2>&1
status=$?
sed "s|$directory|TMPDIR|g" "$directory/out"
echo "rc=$status"

echo "== a bash shebang accepts all three:"
"$BIN" --no-traces -I "$directory/bash-script" > "$directory/out" 2>&1
status=$?
sed "s|$directory|TMPDIR|g" "$directory/out"
echo "rc=$status"

echo "== the shebang is unread without mimicry:"
"$BIN" --no-traces "$directory/sh-script" > "$directory/out" 2>&1
status=$?
sed "s|$directory|TMPDIR|g" "$directory/out"
echo "rc=$status"

echo "== an explicit bash mood wins over an sh shebang:"
"$BIN" --no-traces -I --mood bash "$directory/sh-script" > "$directory/out" 2>&1
status=$?
sed "s|$directory|TMPDIR|g" "$directory/out"
echo "rc=$status"

echo "== an explicit sh mood wins over a bash shebang:"
"$BIN" --no-traces -I --mood sh "$directory/bash-script" > "$directory/out" 2>&1
status=$?
sed "s|$directory|TMPDIR|g" "$directory/out"
echo "rc=$status"

echo "== a mimicked script run as a command reads the same shebang:"
"$BIN" --no-traces -I -c '"$1"' shell "$directory/sh-script" \
    > "$directory/out" 2>&1
status=$?
sed "s|$directory|TMPDIR|g" "$directory/out"
echo "rc=$status"

echo "== the EXIT trap reaches every mood:"
"$BIN" --no-traces -I --mood sh -c 'trap "echo sh-exit" EXIT; echo sh-body'
echo "rc=$?"
"$BIN" --no-traces -I --mood bash -c 'trap "echo bash-exit" EXIT; echo bash-body'
echo "rc=$?"
