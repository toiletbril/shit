unset SHIT_FLAGS
# The env utility applies its NAME=value assignments to the environment and then
# runs the remaining operands as a command directly, so a builtin sees the
# assignment in place and an unresolved name fails with status 127 the way a bare
# command word does. The assignment does not leak past the command.
echo "== env passes an assignment to the command:"
"$BIN" -c "shitbox env GREETING=hello printenv GREETING" </dev/null

echo "== env runs a shell builtin with the assignment applied:"
"$BIN" -c "shitbox env MSG=ok echo done" </dev/null

echo "== the assignment does not leak past the command:"
"$BIN" -c "shitbox env LEAK=1 true; echo \"[\${LEAK-unset}]\"" </dev/null

echo "== an unresolved command fails with status 127:"
"$BIN" -c "shitbox env X=1 no_such_command_xyz123 2>/dev/null; echo status=\$?" </dev/null
