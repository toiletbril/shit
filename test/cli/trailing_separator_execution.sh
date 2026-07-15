unset SHIT_FLAGS

case "$BIN" in
/*) ;;
*) BIN=$(pwd)/$BIN ;;
esac

d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT

printf '#!/bin/sh\nprintf "launched\\n"\n' > "$d/program"
chmod +x "$d/program"
ln -s program "$d/link"
mkdir "$d/directory"
cd "$d" || exit 1

run_rejected_file() {
    label=$1
    source=$2
    output=$("$BIN" -c "$source" 2>&1)
    status=$?
    echo "--- $label ---"
    printf '%s\n' "$output" | grep -F 'This file is not a directory.'
    case "$output" in
    *launched*) echo launched ;;
    *) echo refused ;;
    esac
    echo "rc=$status"
}

run_rejected_file plain './program/'
run_rejected_file exec 'exec ./program/'
run_rejected_file timeout 'shitbox timeout 1 ./program/'
run_rejected_file timeout-pipeline \
    'set -o pipefail; shitbox timeout 1 ./program/ | shitbox cat'
run_rejected_file command 'command ./program/'
run_rejected_file env 'shitbox env ./program/'
run_rejected_file symlink './link/'
run_rejected_file pipeline 'set -o pipefail; ./program/ | shitbox cat'

echo '--- directory remains a directory ---'
output=$("$BIN" --mood sh -c './directory/' 2>&1)
status=$?
case "$output" in
*'This file is not a directory.'*) echo shape-error ;;
*) echo directory-error ;;
esac
echo "rc=$status"

echo '--- missing remains missing ---'
"$BIN" --mood sh -c './missing/' >/dev/null 2>&1
echo "rc=$?"
