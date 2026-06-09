#!/bin/sh
# Tilde expansion of a named user, compared byte-for-byte against dash. Both
# shells resolve the same system users, so the output is machine-independent for
# users present in the local database such as root.
echo ~root
echo ~root/sub/dir
echo ~nouser123
echo "~root"
x=~root
echo "$x"
echo a~root
echo ~root:~root
echo ~
