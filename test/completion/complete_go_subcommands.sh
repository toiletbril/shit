# Go lists its subcommands under a "The commands are:" header followed by a
# blank line, a shape the subcommand scanner did not read. The header is now
# recognized and a blank line after the header no longer closes the section,
# so go subcommands complete. A fake binary named go in a trusted directory
# keeps the candidates stable across machines.
dir=/tmp/shit_go_sub
rm -rf "$dir"
mkdir -p "$dir"
chmod 755 "$dir"
cat > "$dir/go" <<'SH'
#!/bin/sh
cat <<'HELP'
Go is a tool for managing Go source code.

Usage:

	go <command> [arguments]

The commands are:

	bug         start a bug report
	build       compile packages and dependencies
	clean       remove object files and cached files
	doc         show documentation for package or symbol
	env         print Go environment information
HELP
SH
chmod +x "$dir/go"
echo "== subcommands in subcommand position:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'go bu' </dev/null
echo "== subcommands with no prefix:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'go ' </dev/null
rm -rf "$dir"
