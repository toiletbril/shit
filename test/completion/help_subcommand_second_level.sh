# An allowlisted command in a trusted directory whose top-level --help lists
# subcommands and whose "<subcommand> --help" lists that subcommand's own
# options and sub-subcommands. Completion forks the second level when the
# settled second word names a parsed subcommand. A fake binary named for an
# allowlisted command keeps the candidates stable across machines.
dir=/tmp/shit_help_second_level
rm -rf "$dir"
mkdir -p "$dir"
chmod 755 "$dir"
cat > "$dir/tailscale" <<'SH'
#!/bin/sh
if [ "$1" = "set" ]; then
  cat <<'HELP'
USAGE
  tailscale set [flags]

FLAGS
  --hostname     Set the device hostname
  --shields-up   Block incoming connections
HELP
elif [ "$1" = "debug" ]; then
  cat <<'HELP'
SUBCOMMANDS
  derp-map     Print the DERP map
  prefs        Print the prefs
HELP
else
  cat <<'HELP'
SUBCOMMANDS
  up       Connect to the network
  set      Change a preference
  debug    Debugging commands
HELP
fi
SH
chmod +x "$dir/tailscale"
echo "== base subcommands:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'tailscale ' </dev/null
echo "== second-level options from 'tailscale set --help':"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'tailscale set --' </dev/null
echo "== second-level sub-subcommands from 'tailscale debug --help':"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'tailscale debug ' </dev/null
echo "== an unknown second word does not fork:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'tailscale bogus --' </dev/null
rm -rf "$dir"
