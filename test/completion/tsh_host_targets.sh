case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
home=$(mktemp -d)
trap 'rm -rf "$home"' EXIT
mkdir -p "$home/.ssh"
printf 'Host alpha\nHost bravo\nHost gamma\n' > "$home/.ssh/config"
echo "== tsh does not complete an ssh host by prefix, it is the Teleport client:"
HOME="$home" PATH=/nonexistent "$BIN" --debug-complete-at 'tsh br' </dev/null
echo "== tsh ssh does not complete an ssh host either:"
HOME="$home" PATH=/nonexistent "$BIN" --debug-complete-at 'tsh ssh br' </dev/null
echo "== ssh still completes its host by prefix:"
HOME="$home" "$BIN" --debug-complete-at 'ssh br' </dev/null
