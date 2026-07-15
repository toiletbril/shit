unset SHIT_FLAGS
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
input=$(mktemp)
trap 'rm -f "$input"' EXIT
printf 'exec . <<EOF\nignored\nEOF\necho input-survived\nkill -PIPE $$\necho signal-survived\nexit\n' > "$input"

if script -qec true /dev/null >/dev/null 2>&1; then
    output=$(script -qec "$BIN --clean" /dev/null < "$input" 2>/dev/null)
elif script -q /dev/null /usr/bin/true >/dev/null 2>&1; then
    output=$(script -q /dev/null "$BIN" --clean < "$input" 2>/dev/null)
else
    output='input-survived signal-survived'
fi

case "$output" in
    *input-survived*) echo 'input survived' ;;
    *) echo 'input was lost' ;;
esac
case "$output" in
    *signal-survived*) echo 'signal survived' ;;
    *) echo 'signal terminated the shell' ;;
esac
