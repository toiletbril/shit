# git groups its subcommands under left-margin headers such as
# "start a working area (see also: git help tutorial)" followed by indented
# name<spaces>description lines. The section scanner closed the section at the
# grouped header because it sits at the left margin and is not a recognized
# section opener, so the indented entries were skipped. A grouped header now
# keeps an open section intact. A fake binary named go in a trusted directory
# reuses the allowlisted help-fork path, and a git-shaped help body keeps the
# candidates stable across machines.
dir=$(pwd)/completion/complete_git_grouped_subcommands
echo "== grouped subcommands with no prefix:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'go ' </dev/null
echo "== grouped subcommands with a prefix:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'go di' </dev/null
