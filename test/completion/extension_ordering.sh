# A known utility floats the files it operates on to the top of its argument
# completion, matched by extension case insensitively, while everything else
# follows in the normal sorted order. Nothing is hidden, so a file the table did
# not anticipate is still offered. A program with no table entry is unaffected.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: > "$dir/archive.zip"
: > "$dir/ARCHIVE.ZIP"
: > "$dir/backup.tar.gz"
: > "$dir/notes.txt"
: > "$dir/data.csv"
mkdir "$dir/sub"
export PATH="$dir"
cd "$dir"
echo "== unzip floats .zip files first (case insensitive), rest still listed:"
"$BIN" --debug-complete-at 'unzip ' </dev/null
echo "== tar floats the archive, .zip stays in the rest:"
"$BIN" --debug-complete-at 'tar ' </dev/null
echo "== a program with no table entry keeps plain alphabetical order:"
"$BIN" --debug-complete-at 'cat ' </dev/null
