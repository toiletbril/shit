#!/bin/sh
# read rejects the bash-only options under dash, so a read -n fails, and let is
# not a builtin under dash, so it is reported not found.
echo x | read -n 1 y; echo "read_n=$?"
let x=1 2>/dev/null; echo "let=$?"
