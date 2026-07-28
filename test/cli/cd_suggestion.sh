#!/bin/sh

unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

d=$(mktemp -d) || exit 1
start=$PWD
trap 'cd "$start" && [ -n "$d" ] && rm -rf "$d"' EXIT
mkdir "$d/source" "$d/project" "$d/project/nested" "$d/actual-directory"
mkdir "$d/expanded-parent"
mkdir "$d/space dir"
ln -s actual-directory "$d/linked-directory"
ln -s project "$d/logical"
printf 'data\n' > "$d/regular-file"

cd "$d" || exit 1

echo '== transposition:'
"$BIN" -c 'cd soruce' 2>&1 | sed 's|\\|/|g'

echo '== nested path:'
"$BIN" -c 'cd project/nseted' 2>&1 | sed 's|\\|/|g'

echo '== directory symlink:'
"$BIN" -c 'cd linked-directroy' 2>&1 | sed 's|\\|/|g'

echo '== quoted suggestion:'
"$BIN" -c 'cd "space dri"' 2>&1 | sed 's|\\|/|g'

echo '== regular file is excluded:'
"$BIN" -c 'cd regular-fiel' 2>&1 | sed 's|\\|/|g'

echo '== distant miss:'
"$BIN" -c 'cd nowhere-near' 2>&1 | sed 's|\\|/|g'

echo '== first missing expanded component:'
BASE="$d" "$BIN" -c 'cd "$BASE/expanded-parent/miss/tail"' 2>&1 | sed 's|\\|/|g'

echo '== intermediate regular file:'
"$BIN" -c 'cd regular-file/child' 2>&1 | sed 's|\\|/|g'

echo '== direct regular file:'
"$BIN" -c 'cd regular-file' 2>&1 | sed 's|\\|/|g'

echo '== option terminator and missing tail:'
"$BIN" -c 'cd -- project/nseted/tail' 2>&1 | sed 's|\\|/|g'

echo '== one-byte missing component:'
"$BIN" -c 'cd x/tail' 2>&1 | sed 's|\\|/|g'

echo '== dot traversal:'
"$BIN" -c 'cd project/../miss/tail' 2>&1 | sed 's|\\|/|g'

echo '== logical symlink traversal:'
"$BIN" -c 'cd -L logical/nseted/tail' 2>&1 | sed 's|\\|/|g'

echo '== analyzed command path first missing component:'
"$BIN" -c './project/miss/tail' 2>&1 | sed 's|\\|/|g'

echo '== runtime command path first missing component:'
"$BIN" --no-diagnostics -c './project/miss/tail' 2>&1 | sed 's|\\|/|g'

echo '== exec command path first missing component:'
"$BIN" --no-diagnostics -c 'exec ./project/miss/tail' 2>&1 | sed 's|\\|/|g'

if [ "${OS-}" = Windows_NT ]; then
    path_separator='\'
else
    path_separator='/'
fi
native_operand="project${path_separator}nseted${path_separator}tail"
echo '== native separator:'
"$BIN" -c "cd '$native_operand'" 2>&1 | sed 's|\\|/|g'
