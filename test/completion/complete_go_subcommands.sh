# Go lists its subcommands under a "The commands are:" header followed by a
# blank line, a shape the subcommand scanner did not read. The header is now
# recognized and a blank line after the header no longer closes the section,
# so go subcommands complete. A fake binary named go in a trusted directory
# keeps the candidates stable across machines.
dir=$(pwd)/completion/complete_go_subcommands
echo "== subcommands in subcommand position:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'go bu' </dev/null
echo "== subcommands with no prefix:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'go ' </dev/null
