# An allowlisted command in a trusted directory that lists subcommands under a
# bare SUBCOMMANDS header with no colon, the shape tailscale uses without a
# manpage, completes those subcommands in subcommand position. A fake binary
# named for an allowlisted command keeps the candidates stable across machines.
dir=/tmp/shit_help_sub_no_colon
rm -rf "$dir"
mkdir -p "$dir"
chmod 755 "$dir"
cat > "$dir/tailscale" <<'SH'
#!/bin/sh
cat <<'HELP'
The easiest way to fake WireGuard.

USAGE
  tailscale [flags] <subcommand> [command flags]

SUBCOMMANDS
  up           Connect to the thing
  down         Disconnect from the thing
  set          Change a preference
  exit-node    Show exit nodes

FLAGS
  --help       Show help
HELP
SH
chmod +x "$dir/tailscale"
echo "== subcommands under a colon-less header:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'tailscale ' </dev/null
echo "== a flags header at the margin ends the section:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'tailscale -' </dev/null
rm -rf "$dir"
