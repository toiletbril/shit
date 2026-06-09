#!/bin/bash
# Bash [[ ]] file tests, checked byte-for-byte against bash. Covers -e -r -w on
# /dev/null and the negation of -f and -d there, then -e -f -s -d -r -w -x on a
# regular file, a directory, and an empty file made under a private temporary
# directory that the script removes before exit. The directory path is never
# printed, so the output stays deterministic.
[[ -e /dev/null ]] && echo dev-e
[[ -r /dev/null ]] && echo dev-r
[[ -w /dev/null ]] && echo dev-w
[[ -f /dev/null ]] || echo dev-not-f
[[ -d /dev/null ]] || echo dev-not-d
d=$(mktemp -d)
mkdir -p "$d/sub"
echo content > "$d/file"
: > "$d/empty"
chmod 0644 "$d/file"
[[ -e "$d/file" ]] && echo f-e
[[ -f "$d/file" ]] && echo f-f
[[ -s "$d/file" ]] && echo f-s
[[ -s "$d/empty" ]] || echo empty-not-s
[[ -d "$d/sub" ]] && echo d-d
[[ -f "$d/sub" ]] || echo sub-not-f
[[ -r "$d/file" ]] && echo f-r
[[ -w "$d/file" ]] && echo f-w
[[ -x "$d/file" ]] || echo f-not-x
chmod 0755 "$d/file"
[[ -x "$d/file" ]] && echo f-x
rm -rf "$d"
[[ -e "$d/file" ]] || echo gone
