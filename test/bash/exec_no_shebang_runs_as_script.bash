#!/bin/bash
# An executable file with no shebang runs as a shell script rather than failing,
# the way bash falls back when execve reports ENOEXEC.
d=$(mktemp -d)
printf 'echo no-shebang-ran\n' > "$d/s"
chmod +x "$d/s"
"$d/s"
echo "exit=$?"
rm -rf "$d"
