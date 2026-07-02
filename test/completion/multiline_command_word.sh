# A command on a later physical line of a multi-line buffer is completed as its
# own command, the way it is on the first line. Before the command-segment slice
# the command word was mis-read as the buffer's first command, so completion fell
# back to files. PATH is pinned to a directory with no executables and a
# completion spec is registered so the candidate set is stable. The cd case shows
# the directories-only filter also follows the command onto the next line.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/adir"
: > "$dir/afile"
export PATH="$dir"
cd "$dir"
echo "== spec command on the first line:"
"$BIN" -c 'complete -W "start stop restart" svc' --debug-complete-at 'svc ' </dev/null
echo "== spec command after a newline:"
"$BIN" -c 'complete -W "start stop restart" svc' --debug-complete-at "$(printf 'echo hi\nsvc ')" </dev/null
echo "== spec command after '&&' then a newline:"
"$BIN" -c 'complete -W "start stop restart" svc' --debug-complete-at "$(printf 'true &&\nsvc ')" </dev/null
echo "== spec command in a loop body on the next line:"
"$BIN" -c 'complete -W "start stop restart" svc' --debug-complete-at "$(printf 'for x in a b; do\nsvc ')" </dev/null
echo "== cd offers only directories on the next line:"
"$BIN" --debug-complete-at "$(printf 'echo hi\ncd ')" </dev/null
