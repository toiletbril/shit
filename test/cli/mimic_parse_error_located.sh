unset SHIT_FLAGS
# A script run as a command hits ENOEXEC and is mimicked in-process, so a parse
# error in it locates against the script file and name, not the command line the
# way it used to caret an unrelated column of the typed path.
dir=$(mktemp -d)
canonical_dir=$(cd "$dir" && pwd -P)
trap 'rm -rf "$dir"' EXIT
printf '{\n  echo hi\n' > "$dir/unterminated"
chmod +x "$dir/unterminated"
"$BIN" --no-traces -c "$dir/unterminated" 2>&1 | sed 's|\\|/|g' | sed "s|$dir|TMPDIR|g"
echo "rc=$?"

printf 'printf "incremental reached\\n"\nexit 0\n(\n' > "$dir/incremental"
chmod +x "$dir/incremental"
"$BIN" --no-traces -c "$dir/incremental"
echo "incremental rc=$?"

printf '\0binary\n' > "$dir/binary"
chmod +x "$dir/binary"
(cd "$dir" && "$BIN" --no-traces -c ./binary) > "$dir/binary-output" 2>&1
binary_status=$?
sed 's|\\|/|g' "$dir/binary-output" | sed "s|$canonical_dir|TMPDIR|g" | sed "s|$dir|TMPDIR|g"
echo "binary rc=$binary_status"
