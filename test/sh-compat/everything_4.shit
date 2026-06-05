#!/bin/sh
# Redirection, descriptor duplication, and pipelines together.

tmp=/tmp/shit_everything_redir_$$

echo first >"$tmp"
echo second >>"$tmp"
echo "file has:"
cat "$tmp"
echo "line count: $(wc -l <"$tmp")"

sort "$tmp" >"$tmp.sorted"
echo "sorted:"
cat "$tmp.sorted"

errors=$(sh -c 'echo good; echo bad >&2' 2>&1 | sort)
echo "merged: $errors"

count=$(ls /no_such_path_here 2>&1 | grep -c .)
if [ "$count" -ge 1 ]; then
    echo "captured an error line"
fi

ls /no_such_path_here 2>/dev/null
echo "after silenced error"

rm -f "$tmp" "$tmp.sorted"
