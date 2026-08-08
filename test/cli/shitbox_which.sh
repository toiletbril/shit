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
normalized_d=$(printf '%s\n' "$d" | tr '\\' '/')
echo "== a program resolved in a temp PATH:"
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$d" \
    "$BIN" -c 'shitbox which mytool' | tr '\\' '/' | sed "s#$normalized_d#TMPDIR#"
echo "== -a lists every match in the temp PATH:"
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$d" \
    "$BIN" -c 'shitbox which -a mytool' | tr '\\' '/' | sed "s#$normalized_d#TMPDIR#"
rm -rf "$d"

unset SHIT_FLAGS
# which -q prints nothing and reports only through the status.
echo "== -q on a resolving name is silent, status 0:"
"$BIN" -c 'shitbox which -q sh; echo "status=$?"' 2>&1
echo "== -q on a missing name is silent, status 1:"
"$BIN" -c 'shitbox which -q no_such_program_zzz; echo "status=$?"' 2>&1
echo "== without -q the location still prints:"
"$BIN" -c 'shitbox which sh' 2>&1 | grep -Ec '[/\\]sh(\.exe)?$'
