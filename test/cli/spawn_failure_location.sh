#!/bin/sh

unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT
printf 'data\n' > "$d/notexec"
chmod -x "$d/notexec"
real_d=$(CDPATH= cd -- "$d" && pwd -P)

echo '== direct command:'
(
    cd "$d" || exit 1
    "$BIN" --mood sh -c './notexec; printf "rc=%s\n" "$?"'
) 2>&1 | sed "s#$real_d#TMPDIR#g"

printf '\177ELF\002\001\001\000binary\n' > "$d/foreign"
chmod +x "$d/foreign"
echo '== foreign binary:'
(
    cd "$d" || exit 1
    "$BIN" --mood sh -c './foreign; printf "rc=%s\n" "$?"'
) 2>&1 | sed "s#$real_d#TMPDIR#g"

echo '== pipeline stage:'
(
    cd "$d" || exit 1
    "$BIN" --mood sh -c './notexec | shitbox cat; printf "pipeline=%s first=%s\n" "$?" "${PIPESTATUS[0]}"'
) 2>&1 | sed "s#$real_d#TMPDIR#g"

echo '== exec command:'
out=$(
    cd "$d" || exit 1
    "$BIN" --mood sh -c 'exec ./notexec' 2>&1
)
rc=$?
printf '%s\n' "$out" | sed "s#$real_d#TMPDIR#g"
echo "rc=$rc"

echo '== script argument is a directory:'
mkdir "$d/scriptdir"
ln -s "$BIN" "$d/shell"
out=$(cd "$d" && ./shell scriptdir 2>&1)
rc=$?
printf '%s\n' "$out"
echo "rc=$rc"

echo '== recursive mimicked script:'
printf '#!/bin/sh\n"$0"\n' > "$d/recurse"
chmod +x "$d/recurse"
out=$(SHIT_FLAGS= "$BIN" --mood bash -I "$d/recurse" 2>&1)
rc=$?
printf '%s\n' "$out" |
    sed -e "s#$real_d#TMPDIR#g" -e "s#$d#TMPDIR#g" \
        -e '/trace location:/,/here\.$/d'
echo "rc=$rc"
