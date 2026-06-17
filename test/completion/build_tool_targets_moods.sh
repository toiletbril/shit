# The build-tool stage answers in the bash mood as it does in the default mood,
# and the sh mood stays plain. A bare "make <tab>" lists targets under --mood
# bash and offers nothing under --mood sh.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf 'all: build\n\nbuild:\n\ttrue\n\nclean:\n\ttrue\n\n.PHONY: clean\n' > "$dir/Makefile"
cd "$dir"
echo "== bash mood lists targets:"
"$BIN" --mood bash --debug-complete-at 'make ' </dev/null
echo "== bash mood by prefix:"
"$BIN" --mood bash --debug-complete-at 'make bu' </dev/null
echo "== sh mood stays plain:"
"$BIN" --mood sh --debug-complete-at 'make ' </dev/null
