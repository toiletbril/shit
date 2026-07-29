# An allowlisted command in a trusted directory that lists subcommands under a
# Commands header in its --help text, the shape cargo uses without a manpage,
# completes those subcommands in subcommand position and its options after a
# dash. A fake binary named for an allowlisted command keeps the candidates
# stable across machines.
dir=$(pwd)/completion/complete_help_subcommands
echo "== subcommands in subcommand position:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'cargo c' </dev/null
echo "== option after a dash:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'cargo --v' </dev/null
