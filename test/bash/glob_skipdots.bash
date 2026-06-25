#!/bin/bash
# globskipdots, on by default since bash 5.3, keeps . and .. out of a dot glob.
# The temp directory holds one hidden and one plain entry, so the .* glob lists
# the hidden entry alone and the * glob lists the plain one. With globskipdots
# off the .* glob lists . and .. alongside the hidden entry.
d=$(mktemp -d)
touch "$d/.hidden" "$d/visible"
cd "$d" || exit 1
echo .*
echo *
shopt -u globskipdots
echo .*
cd / || exit 1
rm -rf "$d"
