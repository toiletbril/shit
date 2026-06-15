unset SHIT_FLAGS
# The shitbox which utility names a builtin or prints the PATH location of a
# program, and exits non-zero when nothing resolves. It is invoked through the
# shitbox builtin so it runs the shell's own implementation rather than a system
# which on PATH. The PATH lookups run against a temp directory the test makes, so
# the printed path is stable once the temp prefix is masked.
echo "== a builtin name:"; "$BIN" -c 'shitbox which echo'
echo "== an absent name exits 1 with no output:"
"$BIN" -c 'shitbox which definitely_absent_xyz'; echo "rc=$?"
d=$(mktemp -d); printf '#!/bin/sh\n' > "$d/mytool"; chmod +x "$d/mytool"
echo "== a program resolved in a temp PATH:"
PATH="$d" "$BIN" -c 'shitbox which mytool' | sed "s#$d#TMPDIR#"
echo "== -a lists every match in the temp PATH:"
PATH="$d" "$BIN" -c 'shitbox which -a mytool' | sed "s#$d#TMPDIR#"
rm -rf "$d"
