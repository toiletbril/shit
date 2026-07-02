# The document and media viewers gathered in the completion policy header limit
# an empty argument to the file types they open plus directories. A pdf viewer
# shows a .pdf, an image viewer an image. A typed prefix keeps every match and
# only floats the opened type first.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: > "$dir/book.pdf"
: > "$dir/photo.png"
: > "$dir/notes.txt"
: > "$dir/data.csv"
mkdir "$dir/sub"
export PATH="$dir"
cd "$dir"
echo "== mupdf with an empty argument shows only the pdf and directories:"
"$BIN" --debug-complete-at 'mupdf ' </dev/null
echo "== evince with an empty argument shows only the pdf and directories:"
"$BIN" --debug-complete-at 'evince ' </dev/null
echo "== feh with an empty argument shows only the image and directories:"
"$BIN" --debug-complete-at 'feh ' </dev/null
echo "== a typed prefix keeps a non-matching file too:"
"$BIN" --debug-complete-at 'mupdf note' </dev/null
