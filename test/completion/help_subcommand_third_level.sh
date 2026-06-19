# An allowlisted command whose subcommand chain runs three deep. The top-level
# --help lists subcommands, the second level lists its own sub-subcommands, and
# the third level lists that sub-subcommand's options. Completion forks each
# level when the settled words name a parsed subcommand chain, capped at the
# depth limit. A fake binary named for an allowlisted command keeps the
# candidates stable across machines.
dir=/tmp/shit_help_third_level
rm -rf "$dir"
mkdir -p "$dir"
chmod 755 "$dir"
cat > "$dir/docker" <<'SH'
#!/bin/sh
if [ "$1" = "compose" ] && [ "$2" = "config" ]; then
  cat <<'HELP'
OPTIONS
  --services   List the services
  --volumes    List the volumes
HELP
elif [ "$1" = "compose" ]; then
  cat <<'HELP'
Commands:
  up        Start the services
  config    Show the resolved config
HELP
else
  cat <<'HELP'
Commands:
  compose   Run a compose file
  run       Run a command
HELP
fi
SH
chmod +x "$dir/docker"
echo "== first-level subcommands:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'docker ' </dev/null
echo "== second-level sub-subcommands from 'docker compose --help':"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'docker compose ' </dev/null
echo "== third-level options from 'docker compose config --help':"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'docker compose config --' </dev/null
echo "== an unknown deeper word does not fork:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'docker compose bogus --' </dev/null
rm -rf "$dir"
