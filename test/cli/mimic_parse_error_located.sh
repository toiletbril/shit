unset SHIT_FLAGS
# A script run as a command hits ENOEXEC and is mimicked in-process, so a parse
# error in it locates against the script file and name, not the command line the
# way it used to caret an unrelated column of the typed path.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf '{\n  echo hi\n' > "$dir/unterminated"
chmod +x "$dir/unterminated"
"$BIN" -c "$dir/unterminated" 2>&1 | sed "s|$dir|TMPDIR|g"
echo "rc=$?"
