unset SHIT_FLAGS
# which names a builtin or prints the PATH location of a program, and exits
# non-zero when nothing resolves. The PATH lookups run against a temp directory
# the test makes, so the printed path is stable once the temp prefix is masked.
echo "== a builtin name:"; "$BIN" -c 'which echo'
echo "== an absent name exits 1 with no output:"
"$BIN" -c 'which definitely_absent_xyz'; echo "rc=$?"
d=$(mktemp -d); printf '#!/bin/sh\n' > "$d/mytool"; chmod +x "$d/mytool"
echo "== a program resolved in a temp PATH:"
PATH="$d" "$BIN" -c 'which mytool' | sed "s#$d#TMPDIR#"
echo "== -a lists every match in the temp PATH:"
PATH="$d" "$BIN" -c 'which -a mytool' | sed "s#$d#TMPDIR#"
rm -rf "$d"
