set -e

case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
directory=$(mktemp -d)
trap 'rm -rf "$directory"' EXIT
cd "$directory"

result=$("$BIN" --debug-highlight-at \
    'echo https://example.com/a user@host:/dir/file ./missing:part C:/missing')
tab=$(printf '\t')

printf '%s\n' "$result" | grep -E "${tab}(url|invalid-path)$"
