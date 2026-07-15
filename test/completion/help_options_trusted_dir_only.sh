# Option completion from a command's --help text passes two gates. The command
# is on shit's allowlist of commands safe to fork, and it resolves into a
# directory the current user or root owns that is not writable by group or
# other. A fake binary named for an allowlisted command (cargo) drives the
# probe deterministically. The same binary under a name shit does not recognize,
# or in a world-writable directory, is never forked.
trusted=/tmp/shit_help_trusted
untrusted=/tmp/shit_help_untrusted
rm -rf "$trusted" "$untrusted"
mkdir -p "$trusted" "$untrusted"
chmod 755 "$trusted"
chmod 777 "$untrusted"
write_probe() {
  cat > "$1" <<'SH'
#!/bin/sh
echo forked >> /tmp/shit_help_marker
echo "  --marker-option   a probe option"
SH
  chmod +x "$1"
}
write_probe "$trusted/cargo"
write_probe "$trusted/helpprobe"
write_probe "$untrusted/cargo"

rm -f /tmp/shit_help_marker
echo "== allowlisted command in a trusted directory offers its --help options:"
PATH="$trusted:$PATH" "$BIN" --debug-complete-at 'cargo --mark' </dev/null
echo "== and was forked:"
if [ -f /tmp/shit_help_marker ]; then echo "forked"; else echo "not forked"; fi

rm -f /tmp/shit_help_marker
echo "== a command not on the allowlist is never forked, even when trusted:"
PATH="$trusted:$PATH" "$BIN" --debug-complete-at 'helpprobe --mark' </dev/null
echo "== and was never forked:"
if [ -f /tmp/shit_help_marker ]; then echo "forked"; else echo "not forked"; fi

rm -f /tmp/shit_help_marker
echo "== an allowlisted command in a world-writable directory is never forked:"
PATH="$untrusted:$PATH" "$BIN" --debug-complete-at 'cargo --mark' </dev/null
echo "== and was never forked:"
if [ -f /tmp/shit_help_marker ]; then echo "forked"; else echo "not forked"; fi

cat > "$trusted/cargo" <<'SH'
#!/bin/sh
echo attempted >> /tmp/shit_help_marker
sleep 2
SH
chmod +x "$trusted/cargo"
rm -f /tmp/shit_help_marker
echo "== a timed out help command is attempted once:"
PATH="$trusted:$PATH" "$BIN" --debug-complete-at 'cargo --mark' </dev/null
test "$(wc -l < /tmp/shit_help_marker)" -eq 1 && echo "attempted once"

rm -rf "$trusted" "$untrusted" /tmp/shit_help_marker
