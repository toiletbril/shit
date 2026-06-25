#!/bin/bash
# The dotglob and nullglob shopts steer the glob, checked byte-for-byte against
# bash. With dotglob off a leading-dot-less pattern skips the hidden entry, with
# it on the hidden entry joins the match. With nullglob off a pattern with no
# match stays literal, with it on the pattern expands to nothing.
d=$(mktemp -d)
touch "$d/.hidden" "$d/visible"
cd "$d" || exit 1
echo "plain: "*
shopt -s dotglob
echo "dotglob: "*
shopt -u dotglob
echo "nomatch_literal: "nope*
shopt -s nullglob
echo "nomatch_null: "nope*
echo "dotglob_null: "*
shopt -u nullglob
cd / || exit 1
rm -rf "$d"
