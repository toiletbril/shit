#!/bin/sh

unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT
mkdir "$d/source" "$d/project" "$d/project/nested" "$d/actual-directory"
mkdir "$d/space dir"
ln -s actual-directory "$d/linked-directory"
printf 'data\n' > "$d/regular-file"

cd "$d" || exit 1

echo '== transposition:'
"$BIN" -c 'cd soruce' 2>&1

echo '== nested path:'
"$BIN" -c 'cd project/nseted' 2>&1

echo '== directory symlink:'
"$BIN" -c 'cd linked-directroy' 2>&1

echo '== quoted suggestion:'
"$BIN" -c 'cd "space dri"' 2>&1

echo '== regular file is excluded:'
"$BIN" -c 'cd regular-fiel' 2>&1

echo '== distant miss:'
"$BIN" -c 'cd nowhere-near' 2>&1
