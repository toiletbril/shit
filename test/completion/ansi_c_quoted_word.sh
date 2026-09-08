# A word that opens with $'...' is decoded through the ANSI C escapes before the
# candidates are looked up. A word that opens with $"..." is decoded the same
# way and its body completes as a double quoted word. PATH is pinned to one
# directory so only the marker names join the candidates.
dir=$(mktemp -d) || exit 1
trap '[ -n "$dir" ] && "$TEST_SYSTEM_RM" -rf -- "$dir"' EXIT
: > "$dir/ZQmarker_file"
mkdir "$dir/ZQdir_marker"
: > "$dir/ZQdir_marker/ZQinner"
printf '#!/bin/sh\n' > "$dir/ZQcommand_marker"
chmod +x "$dir/ZQcommand_marker"
export PATH="$dir"
cd "$dir"
echo "== a plain single quote lists the markers:"
"$BIN" --debug-complete-at "echo 'ZQ" </dev/null
echo "== an ANSI C quote lists the same markers:"
"$BIN" --debug-complete-at "echo \$'ZQ" </dev/null
echo "== an ANSI C escape resolves to the decoded name:"
"$BIN" --debug-complete-at "echo \$'ZQdir_marker/\\x5a" </dev/null
echo "== an ANSI C quote completes a command name:"
"$BIN" --debug-complete-at "\$'ZQcommand" </dev/null
echo "== an ANSI C quote after a directory keeps the prefix:"
"$BIN" --debug-complete-at "echo ZQdir_marker/\$'" </dev/null
echo "== a doubled dollar sign keeps the process id in the prefix:"
"$BIN" --debug-complete-at "echo \$\$'ZQ" </dev/null
echo "== a tripled dollar sign keeps the process id before the quote body:"
"$BIN" --debug-complete-at "echo \$\$\$'ZQ" </dev/null
echo "== a locale quote lists the same markers:"
"$BIN" --debug-complete-at "echo \$\"ZQ" </dev/null
echo "== an escaped quote stays inside a locale quoted word:"
"$BIN" --debug-complete-at "echo \$\"ZQ\\\" marker ZQ" </dev/null
echo "== a locale quote completes a command name:"
"$BIN" --debug-complete-at "\$\"ZQcommand" </dev/null
echo "== a locale quote after a directory keeps the prefix:"
"$BIN" --debug-complete-at "echo ZQdir_marker/\$\"" </dev/null
echo "== a doubled dollar sign keeps the process id before a locale quote:"
"$BIN" --debug-complete-at "echo \$\$\"ZQ" </dev/null
