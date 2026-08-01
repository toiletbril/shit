set -e

BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
dir=$(mktemp -d)
trap '[ -n "$dir" ] && /bin/rm -rf "$dir"' EXIT
mkdir "$dir/adir"
: > "$dir/afile"
ln -s afile "$dir/alink"
cd "$dir"

result=$("$BIN" --debug-highlight-at \
    'cd adir; cd afile; ls afile; ./afile/; ./alink/; ./adir/')
tab=$(printf '\t')
printf '%s\n' "$result" | grep -E "${tab}(existing-path|invalid-path)$"

mkdir -p "$dir/native/subdir"
if [ "${OS-}" = Windows_NT ]; then
    native_source='native\\subdir'
    "$BIN" --debug-highlight-at "echo $native_source" |
        grep -F "${native_source}${tab}existing-path" >/dev/null
    printf 'native/subdir\texisting-path\n'
else
    native_source='native/subdir'
    "$BIN" --debug-highlight-at "echo $native_source" |
        grep -F "${native_source}${tab}existing-path"
fi

mkdir -p "$dir/[quoted] path/child" "$dir/Library/Application"
: > "$dir/space dir tool"
mkdir -p "$dir/space dir/child" "$dir/~/local-only" "$dir/literal-home"
: > "$dir/space dir/tool"
chmod +x "$dir/space dir/tool"
mixed=$(
    "$BIN" --debug-highlight-at "cd '[quoted] path'/chi"
    "$BIN" --debug-highlight-at "cd '[quoted] path'/child"
    "$BIN" --debug-highlight-at "cat '[quoted] path'/*"
    HOME="$dir" "$BIN" --debug-highlight-at "cat ~/'Library'/Appl"
    "$BIN" --debug-highlight-at 'cd space\ dir/chi'
    "$BIN" --debug-highlight-at './space\ dir/tool'
    HOME="$dir/literal-home" "$BIN" --debug-highlight-at "cd '~'/loc"
    "$BIN" --debug-highlight-at 'echo \* \? \['
)
printf '%s\n' "$mixed" |
    grep -E "${tab}(string|existing-path|partial-path|glob)$"
