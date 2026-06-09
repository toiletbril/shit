#!/bin/bash
# Bash default, assign, and alternate expansion forms, checked byte-for-byte
# against bash. Covers the colon forms that treat an empty value as unset and
# the plain forms that treat only an unset value, plus the side effect of
# ${v:=x} which assigns back into the variable.
unset u
echo "${u:-fallback}"
echo "${u-fallback}"
echo "[${u}]"
set_empty=
echo "[${set_empty:-fb}]"
echo "[${set_empty-fb}]"
echo "[${u:=assigned}]"
echo "[${u}]"
unset w
echo "[${w:=now}]"
echo "[${w}]"
val=present
echo "${val:+replacement}"
echo "${val:-other}"
unset miss
echo "[${miss:+alt}]"
e=
echo "[${e:+alt}]"
echo "[${e+alt}]"
