unset SHIT_FLAGS
# A braced variable path keeps its source spelling, and a bare reference still
# completes variable names.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir -p "$dir/Downloads" "$dir/Documents"

echo "== \${HOME}/ braced:"
HOME="$dir" "$BIN" --debug-complete-at 'cat ${HOME}/Doc' </dev/null
echo "== a bare variable reference still completes names:"
VARIABLE_COMPLETION_UNIQUE="$dir" \
    "$BIN" --debug-complete-at 'echo $VARIABLE_COMPLETION_UNI' </dev/null
