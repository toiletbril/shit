#!/bin/bash
# Bash command -v for a builtin and a keyword, checked byte-for-byte against
# bash. A builtin and a keyword name print themselves, and a missing name yields
# nothing with status one.
command -v cd
command -v echo
command -v if
command -v while
command -v case
command -v no_such_command_zzz
echo "rc=$?"
