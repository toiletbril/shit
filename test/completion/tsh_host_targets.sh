case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
home=$(mktemp -d)
trap 'rm -rf "$home"' EXIT
mkdir -p "$home/.ssh"
printf 'Host alpha\nHost bravo\nHost gamma\n' > "$home/.ssh/config"
echo "== tsh lists ssh hosts:"
HOME="$home" "$BIN" --debug-complete-at 'tsh ' </dev/null
echo "== tsh by prefix:"
HOME="$home" "$BIN" --debug-complete-at 'tsh br' </dev/null
echo "== ssh agrees:"
HOME="$home" "$BIN" --debug-complete-at 'ssh ' </dev/null
