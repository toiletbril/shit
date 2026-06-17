#!/bin/bash
# cd into a symlinked directory and back out with cd .., the bash -L default. PWD
# keeps the symlink path while pwd -P shows the resolved target, and cd .. returns
# to the directory holding the link rather than the physical parent. Checked byte
# for byte against bash.
base=$(mktemp -d)
mkdir -p "$base/real/sub"
ln -s "$base/real/sub" "$base/link"
cd "$base"
cd link
echo "in_link=$([ "$PWD" = "$base/link" ] && echo yes || echo no)"
echo "physical_differs=$([ "$(pwd -P)" != "$PWD" ] && echo yes || echo no)"
cd ..
echo "after_dotdot=$([ "$PWD" = "$base" ] && echo yes || echo no)"
rm -rf "$base"
