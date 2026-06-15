# The shitbox builtin completes its utility names in the first operand slot and
# each utility's flags after it, all from the registered FLAG lists. A bare
# utility name completes its own flags when the shitbox option resolves it as a
# command.
echo "== shitbox utilities by prefix:"
"$BIN" --debug-complete-at 'shitbox m' </dev/null
echo "== ls flags through shitbox:"
"$BIN" --debug-complete-at 'shitbox ls -' </dev/null
echo "== du flags through shitbox:"
"$BIN" --debug-complete-at 'shitbox du -' </dev/null
echo "== shitbox own flags:"
"$BIN" --debug-complete-at 'shitbox --' </dev/null
echo "== bare utility flags under set -o shitbox:"
"$BIN" -c 'set -o shitbox' --debug-complete-at 'ls -' </dev/null
