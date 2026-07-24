# An allowlisted command in a trusted directory that lists subcommands under a
# bare SUBCOMMANDS header with no colon, the shape tailscale uses without a
# manpage, completes those subcommands in subcommand position. A fake binary
# named for an allowlisted command keeps the candidates stable across machines.
dir=$(mktemp -d) || exit 1
trap '[ -n "$dir" ] && rm -rf "$dir"' EXIT
mkdir -p "$dir"
chmod 755 "$dir"
cat > "$dir/tailscale" <<'SH'
#!/bin/sh
printf '%s\n' \
    'The easiest way to fake WireGuard.' \
    '' \
    'USAGE' \
    '  tailscale [flags] <subcommand> [command flags]' \
    '' \
    'SUBCOMMANDS' \
    '  up           Connect to the thing' \
    '  down         Disconnect from the thing' \
    '  set          Change a preference' \
    '  exit-node    Show exit nodes' \
    '' \
    'FLAGS' \
    '  --help       Show help'
SH
chmod +x "$dir/tailscale"
echo "== subcommands under a colon-less header:"
attempt_count=0
while [ "$attempt_count" -lt 3 ]; do
    subcommands=$(PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'tailscale ' </dev/null)
    case $subcommands in
        *down*) break ;;
    esac
    attempt_count=$((attempt_count + 1))
done
printf '%s\n' "$subcommands"
echo "== a flags header at the margin ends the section:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" --debug-complete-at 'tailscale -' </dev/null
