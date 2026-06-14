# Option completion from a command's --help text forks the command, so it runs
# only for a binary in a directory the current user or root owns that is not
# writable by group or other. A binary in such a directory offers its --help
# options, while the same binary in a world-writable directory is never forked,
# so its --help side effects never run and it offers nothing.
trusted=/tmp/shit_help_trusted
untrusted=/tmp/shit_help_untrusted
rm -rf "$trusted" "$untrusted"
mkdir -p "$trusted" "$untrusted"
chmod 755 "$trusted"
chmod 777 "$untrusted"
for d in "$trusted" "$untrusted"; do
  cat > "$d/helpprobe" <<'SH'
#!/bin/sh
echo forked >> /tmp/shit_help_marker
echo "  --marker-option   a probe option"
SH
  chmod +x "$d/helpprobe"
done

rm -f /tmp/shit_help_marker
echo "== a binary in a trusted directory offers its --help options:"
PATH="$trusted:$PATH" "$BIN" --debug-complete-at 'helpprobe --mark' </dev/null
echo "== and was forked:"
if [ -f /tmp/shit_help_marker ]; then echo "forked"; else echo "not forked"; fi

rm -f /tmp/shit_help_marker
echo "== a binary in a world-writable directory offers nothing:"
PATH="$untrusted:$PATH" "$BIN" --debug-complete-at 'helpprobe --mark' </dev/null
echo "== and was never forked:"
if [ -f /tmp/shit_help_marker ]; then echo "forked"; else echo "not forked"; fi

rm -rf "$trusted" "$untrusted" /tmp/shit_help_marker
