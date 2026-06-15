unset SHIT_FLAGS
# --list-diagnostics carries the shellcheck mirrors and the shell's own
# strictness warnings in two sections.
"$BIN" --list-diagnostics | grep -c '^  SC'
"$BIN" --list-diagnostics | grep -A 100 'STRICTNESS WARNINGS' | grep -c '^  '
"$BIN" --list-diagnostics | grep -Ec 'nounset|failglob|posix-bashism'
echo "rc=$?"
