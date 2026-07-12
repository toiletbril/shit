# git groups its subcommands under left-margin headers such as
# "start a working area (see also: git help tutorial)" followed by indented
# name<spaces>description lines. The section scanner closed the section at the
# grouped header because it sits at the left margin and is not a recognized
# section opener, so the indented entries were skipped. A grouped header now
# keeps an open section intact. A fake binary named go in a trusted directory
# reuses the allowlisted help-fork path, and a git-shaped help body keeps the
# candidates stable across machines.
dir=/tmp/shit_git_grouped
rm -rf "$dir"
mkdir -p "$dir"
chmod 755 "$dir"
cat > "$dir/go" <<'SH'
#!/bin/sh
cat <<'HELP'
usage: go [--version] <command> [<args>]

These are common Go commands used in various situations:

start a working area (see also: go help tutorial)
   clone      Clone a repository into a new directory
   init       Create an empty Go repository

work on the current change (see also: go help everyday)
   add        Add file contents to the index
   mv         Move or rename a file, a directory, or a symlink
   restore    Restore working tree files

examine the history and state (see also: go help revisions)
   diff       Show changes between commits
   log        Show commit logs
   status     Show the working tree status
HELP
SH
chmod +x "$dir/go"
echo "== grouped subcommands with no prefix:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'go ' </dev/null
echo "== grouped subcommands with a prefix:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'go di' </dev/null
rm -rf "$dir"
