#!/bin/bash
# Bash indirect expansion ${!ref} and prefix name listing ${!prefix*} and
# ${!prefix@}, checked byte-for-byte against bash.
target=hello
ref=target
echo "${!ref}"

a=b
b=world
echo "${!a}"

# A chain of two indirections.
p1=p2
p2=final
echo "${!p1}"

# Prefix name listing, sorted.
zoofoo=1
zoobar=2
zoobaz=3
echo ${!zoo*}
echo "${!zoo@}"

# A prefix that matches nothing yields empty.
echo "[${!nomatch_prefix*}]"

# Listing reflects a single match too.
onlyone_x=1
echo ${!onlyone*}
