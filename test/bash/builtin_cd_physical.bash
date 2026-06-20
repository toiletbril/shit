#!/bin/bash
# cd -P resolves the symlinks to the physical directory, while -L and the default
# keep the logical path the operand spelled.
d=$(mktemp -d)
mkdir -p "$d/real"
ln -sfn "$d/real" "$d/link"
echo "physical=$(cd -P "$d/link" && pwd | grep -o '/real$')"
echo "logical=$(cd -L "$d/link" && pwd | grep -o '/link$')"
echo "default=$(cd "$d/link" && pwd | grep -o '/link$')"
rm -rf "$d"
