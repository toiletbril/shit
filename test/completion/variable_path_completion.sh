unset SHIT_FLAGS
# A variable-prefixed path completes by expanding the variable to list the real
# directory, while the offered candidate keeps the literal $NAME prefix so it
# still expands at run time. A tilde stays literal the same way, and a bare
# variable reference still completes variable names.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir -p "$dir/Downloads" "$dir/Documents"

echo "== \$HOME/ prefix stays active:"
HOME="$dir" "$BIN" --debug-complete-at 'cat $HOME/Down' </dev/null
echo "== \${HOME}/ braced:"
HOME="$dir" "$BIN" --debug-complete-at 'cat ${HOME}/Doc' </dev/null
echo "== a bare variable reference still completes names:"
HOME="$dir" "$BIN" --debug-complete-at 'echo $HOM' </dev/null
echo "== a tilde stays literal:"
HOME="$dir" "$BIN" --debug-complete-at 'cat ~/Doc' </dev/null
