# A wrapper command, its dash options, and leading assignments are transparent,
# so the inner command drives both command-position and argument completion,
# the way fish skips sudo. PATH is pinned to an empty directory so a host binary
# such as exportfs cannot join the command candidates.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
export PATH="$dir"
echo "== command position through sudo:"
"$BIN" --debug-complete-at 'sudo expor' </dev/null
echo "== inner builtin args through env and an assignment:"
"$BIN" --debug-complete-at 'env FOO=bar set -o no' </dev/null
echo "== signal names through command:"
"$BIN" --debug-complete-at 'command kill -' </dev/null
