# The build-tool stage lists make targets through the make database, the npm
# family's scripts from package.json, and honors -C, hermetically in a
# temporary directory.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf 'all: build test\n\nbuild:\n\ttrue\n\ntest: build\n\ttrue\n\nclean:\n\ttrue\n\n.PHONY: clean\n' > "$dir/Makefile"
printf '{ "scripts": { "dev": "x", "build": "y", "lint:fix": "z" } }\n' > "$dir/package.json"
cd "$dir"
echo "== make targets:"
"$BIN" --debug-complete-at 'make ' </dev/null
echo "== make by prefix:"
"$BIN" --debug-complete-at 'make cl' </dev/null
echo "== npm run scripts:"
"$BIN" --debug-complete-at 'npm run ' </dev/null
echo "== bun run by prefix:"
"$BIN" --debug-complete-at 'bun run li' </dev/null
echo "== make -C from outside:"
cd /
"$BIN" --debug-complete-at "make -C $dir bu" </dev/null
