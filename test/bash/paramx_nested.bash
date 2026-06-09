#!/bin/bash
# Bash nested parameter expansion, checked byte-for-byte against bash. An inner
# expansion supplies the indirection name, the default value, the removal
# pattern, the substring length, and the replacement text of an outer form.
name=World
greeting=name
echo "${!greeting}"
prefix=pre
fallback=DEFAULT
unset target
echo "${target:-${fallback}}"
echo "${target:-${prefix}fix}"
inner=lo
v=hello
echo "${v%$inner}"
echo "${v#${v}}"
base=file.txt
ext=txt
echo "${base%.$ext}"
len=3
v2=abcdefgh
echo "${v2:0:$len}"
echo "${v2:0:${#v2}}"
pat=l
rep=L
echo "${v//$pat/$rep}"
unset maybe
def=substituted
echo "${maybe:=${def}}"
echo "$maybe"
