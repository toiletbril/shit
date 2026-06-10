#!/bin/bash
# A file the shell found but cannot execute exits with status 126 rather than
# 127, whether it lacks execute permission or carries binary content in its head.
d=$(mktemp -d)
printf 'echo nope\n' > "$d/noperm"
chmod -x "$d/noperm"
"$d/noperm" 2>/dev/null
echo "noperm=$?"
head -c 4 /dev/zero > "$d/bin"
printf 'binary\n' >> "$d/bin"
chmod +x "$d/bin"
"$d/bin" 2>/dev/null
echo "binary=$?"
rm -rf "$d"
