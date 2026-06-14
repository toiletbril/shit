# Option completion from a command's --help text forks the command, so it runs
# only for a binary that resolves into a trusted system directory. A command in
# an untrusted directory, such as one placed in the working directory or a
# temp directory on PATH, is never forked, so its --help side effects never run
# and it offers no options.
dir=/tmp/shit_help_untrusted
rm -rf "$dir"
mkdir -p "$dir"
cat > "$dir/untrustedcmd" <<'SH'
#!/bin/sh
echo forked > /tmp/shit_help_marker
echo "--secret-option"
SH
chmod +x "$dir/untrustedcmd"
rm -f /tmp/shit_help_marker
echo "== untrusted command offers no --help options:"
PATH="$dir:$PATH" "$BIN" --debug-complete-at 'untrustedcmd --s' </dev/null
echo "== and was never forked:"
if [ -f /tmp/shit_help_marker ]; then echo "forked"; else echo "not forked"; fi
rm -rf "$dir" /tmp/shit_help_marker
