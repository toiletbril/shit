# The document and media viewers gathered in the completion policy header float
# the file types they open ahead of the rest, the same soft ordering the archive
# tools use. A pdf viewer floats a .pdf, an image viewer floats an image.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: > "$dir/book.pdf"
: > "$dir/photo.png"
: > "$dir/notes.txt"
: > "$dir/data.csv"
mkdir "$dir/sub"
export PATH="$dir"
cd "$dir"
echo "== mupdf floats the pdf first, the rest still listed:"
"$BIN" --debug-complete-at 'mupdf ' </dev/null
echo "== evince floats the pdf first:"
"$BIN" --debug-complete-at 'evince ' </dev/null
echo "== feh floats the image first:"
"$BIN" --debug-complete-at 'feh ' </dev/null
