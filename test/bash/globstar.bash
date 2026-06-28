#!/bin/bash
# Bash globstar **, checked byte-for-byte against bash. The ** matches across
# directory levels when shopt globstar is on, as a trailing component it lists
# every file and directory recursively, and in a path position it stands in for
# zero or more levels. Without globstar ** behaves like *.
dir=/tmp/shit_globstar_test_$$
rm -rf "$dir"
mkdir -p "$dir/a/b/c" "$dir/a/d"
touch "$dir/f0" "$dir/a/f1" "$dir/a/b/f2" "$dir/a/b/c/f3" "$dir/a/d/f4"
cd "$dir"
shopt -s globstar
echo "--- trailing ---"
for x in **; do echo "$x"; done
echo "--- dirs ---"
for x in **/; do echo "$x"; done
echo "--- middle ---"
for x in a/**/f*; do echo "$x"; done
echo "--- leading ---"
for x in **/f*; do echo "$x"; done
echo "--- base trailing ---"
for x in a/**; do echo "$x"; done
echo "--- off ---"
shopt -u globstar
for x in **; do echo "$x"; done
cd /
rm -rf "$dir"
