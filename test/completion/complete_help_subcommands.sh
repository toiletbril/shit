# An allowlisted command in a trusted directory that lists subcommands under a
# Commands header in its --help text, the shape cargo uses without a manpage,
# completes those subcommands in subcommand position and its options after a
# dash. A fake binary named for an allowlisted command keeps the candidates
# stable across machines.
dir=/tmp/shit_help_sub
rm -rf "$dir"
mkdir -p "$dir"
chmod 755 "$dir"
cat > "$dir/cargo" <<'SH'
#!/bin/sh
cat <<'HELP'
A fake multi-tool

Usage: cargo [OPTIONS] <COMMAND>

Options:
      --verbose   Be loud
  -h, --help      Print help

Commands:
    build, b    Compile the thing
    check       Check the thing for errors
    clean       Remove the output directory
HELP
SH
chmod +x "$dir/cargo"
echo "== subcommands in subcommand position:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'cargo c' </dev/null
echo "== option after a dash:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'cargo --v' </dev/null
rm -rf "$dir"
