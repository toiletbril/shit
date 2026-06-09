#!/bin/sh
# CDPATH resolves a relative cd operand against a directory list, checked against
# dash. Only stable markers are printed, never the pid-specific temporary path.

base=/tmp/shit_cdpath_$$
rm -rf "$base"
mkdir -p "$base/sub" "$base/other"

# A nonempty entry resolves the operand, and the basename confirms the landing.
CDPATH=$base
cd sub >/dev/null 2>&1 && echo "landed=$(basename "$(pwd)")"

# A move through a nonempty entry announces the directory on stdout.
cd "$base" >/dev/null 2>&1
CDPATH=$base
out=$(cd other)
test -n "$out" && echo announce_nonempty

# An empty entry names the current directory and stays silent.
cd "$base" >/dev/null 2>&1
CDPATH=:$base
out=$(cd sub)
test -z "$out" && echo cwd_entry_silent

# A ./ operand skips CDPATH and resolves against the current directory.
cd "$base" >/dev/null 2>&1
CDPATH=$base
cd ./other >/dev/null 2>&1 && echo "dot_landed=$(basename "$(pwd)")"

cd / >/dev/null 2>&1
rm -rf "$base"
echo done
