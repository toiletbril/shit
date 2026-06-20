#!/bin/sh
# cd -P resolves the symlinks to the physical directory under dash too, and the
# dot command ignores trailing operands the way dash does, so the caller's
# positional parameters carry through.
d=$(mktemp -d)
mkdir -p "$d/real"
ln -sfn "$d/real" "$d/link"
echo "physical=$(cd -P "$d/link" && pwd | grep -o '/real$')"
file=$(mktemp)
printf 'echo "in=$1"\n' > "$file"
set -- keep
. "$file" ignored
echo "out=$1"
rm -rf "$d" "$file"
