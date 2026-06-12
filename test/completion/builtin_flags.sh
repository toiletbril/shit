# The builtin flag stage reads the registered FLAG lists, the set option
# table, the shopt names, kill's signal names, and the shell binary's own
# flags, all without a manpage.
echo "== set switches:"
"$BIN" --debug-complete-at 'set -' </dev/null
echo "== set -o names by prefix:"
"$BIN" --debug-complete-at 'set -o no' </dev/null
echo "== shit binary flags:"
"$BIN" --debug-complete-at 'shit --b' </dev/null
echo "== declare letters:"
"$BIN" --debug-complete-at 'declare -' </dev/null
echo "== kill signal names:"
"$BIN" --debug-complete-at 'kill -' </dev/null
echo "== shopt names by prefix:"
"$BIN" --debug-complete-at 'shopt glob' </dev/null
echo "== read flags:"
"$BIN" --debug-complete-at 'read -' </dev/null
