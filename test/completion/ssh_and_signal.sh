# ssh, scp, sftp, and rsync complete host names from ~/.ssh/config, known_hosts,
# and /etc/hosts, dropping wildcard patterns. kill completes the signal names for
# a dashed token. HOME is pointed at a temp config and the prefixes are chosen so
# the /etc/hosts and process lists (which vary per machine) never join the stable
# candidates.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir -p "$dir/.ssh"
cat > "$dir/.ssh/config" <<'CFG'
Host alpha-box beta-box
  HostName 10.0.0.1
Host gamma-wild-*
  User someone
CFG
echo "== ssh completes a config host by prefix:"
HOME="$dir" "$BIN" --debug-complete-at 'ssh alpha' </dev/null
echo "== scp completes the same hosts:"
HOME="$dir" "$BIN" --debug-complete-at 'scp beta' </dev/null
echo "== sftp and rsync complete hosts too:"
HOME="$dir" "$BIN" --debug-complete-at 'sftp alpha' </dev/null
HOME="$dir" "$BIN" --debug-complete-at 'rsync beta' </dev/null
echo "== a wildcard Host pattern is not offered:"
HOME="$dir" "$BIN" --debug-complete-at 'ssh gamma' </dev/null
echo "== kill completes a signal name for a dashed token:"
"$BIN" --debug-complete-at 'kill -TE' </dev/null
echo "== and another signal prefix:"
"$BIN" --debug-complete-at 'kill -HU' </dev/null
