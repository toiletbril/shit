# The make target completion falls back to scanning the Makefile directly when no
# GNU make answers the database probe, so the bundled make still completes
# targets. PATH is emptied for the completing invocation so the probe finds no
# system make.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf 'all: build\n\ttrue\nbuild:\n\ttrue\ntest: build\n\ttrue\nclean:\n\ttrue\n.PHONY: clean\n' > "$dir/Makefile"
cd "$dir"
echo "== make targets without gnu make:"
PATH=/nonexistent "$BIN" --debug-complete-at 'make ' </dev/null
echo "== make by prefix:"
PATH=/nonexistent "$BIN" --debug-complete-at 'make cl' </dev/null
