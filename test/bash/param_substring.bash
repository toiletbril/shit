#!/bin/bash
# Bash substring parameter expansion ${v:offset:length}, checked byte-for-byte
# against bash. Covers positive and negative offsets, negative lengths, an
# arithmetic offset from a variable, and out-of-range clamping.

v=abcdefgh
echo "${v:2}"
echo "${v:2:3}"
echo "${v:0:4}"
echo "${v: -3}"
echo "${v: -3:2}"
echo "${v:2:-1}"
echo "${v:20}"
echo "[${v:8}]"

short=abc
echo "${short:1:10}"

n=2
len=3
echo "${v:n:len}"
echo "${v:n+1:2}"

# The colon modifiers still parse as themselves, not as a substring.
unset u
echo "${u:-default}"
echo "${u:+alt}"
val=set
echo "${val:+yes}"

# A negative offset that reaches before the start yields empty, not the whole
# value.
echo "[${v: -100}]"
echo "[${short: -50}]"


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

# Bash case modification with a glob bracket class selector, checked
# byte-for-byte against bash. The ^^ and ,, forms restrict the conversion to the
# characters that match the trailing pattern.
upper=ABCDEF
echo "${upper,,[ACE]}"
lower=abcdef
echo "${lower^^[bdf]}"
word=banana
echo "${word^^a}"
echo "${word^[b]}"
mixed=AaBbCc
echo "${mixed,,[A-Z]}"
echo "${mixed^^[a-z]}"

# Bash case-modification expansion ${v^} ${v^^} ${v,} ${v,,} with optional glob,
# checked byte-for-byte against bash.
v=hello
echo "${v^}"
echo "${v^^}"
u=HELLO
echo "${u,}"
echo "${u,,}"
echo "${v^^l}"
echo "${v^^[aeiou]}"
m=abcABC
echo "${m,,}"
echo "${m^^}"
n=123abc
echo "${n^^}"
mixed=mIxEd
echo "${mixed^}"
echo "${mixed,}"
phrase="the quick fox"
echo "${phrase^^}"
echo "${phrase^^[tqf]}"

# Bash prefix and suffix pattern removal ${v#p} ${v##p} ${v%p} ${v%%p}, checked
# byte-for-byte against bash. Covers shortest and longest match, glob bracket
# classes, an empty value, and a no-match left unchanged.
path=/usr/local/bin/prog
echo "${path#*/}"
echo "${path##*/}"
echo "${path%/*}"
echo "${path%%/*}"
file=archive.tar.gz
echo "${file%.*}"
echo "${file%%.*}"
echo "${file#*.}"
echo "${file##*.}"
v=hello
echo "${v#x}"
echo "${v%x}"
echo "${v#[hH]}"
echo "${v%[oO]}"
empty=
echo "[${empty#x}]"
url=http://example.com/path
echo "${url#http://}"
echo "${url%/*}"

# The ${var@op} parameter transforms, checked byte-for-byte against bash. Q
# quotes for reuse, U u L change case, E expands escapes, A prints an
# assignment, and a lists the attribute letters.
v="a b"
echo "${v@Q}"
w=abc
echo "${w@Q}"
empty=""
echo "${empty@Q}"
q="a'b"
echo "${q@Q}"
esc=$'\e[1m'
echo "${esc@Q}"
m="Hello World"
echo "${m@U}"
echo "${m@L}"
echo "${m@u}"
e='a\tb\nc'
printf '%s' "${e@E}" | od -An -c | tr -s ' '
echo "${v@A}"
export EV=1
echo "${EV@a}"
readonly RV=1
echo "${RV@a}"
arr=(x y z)
echo "${arr@K}"
echo "${arr@k}"
p="end\$"
echo "${p@P}"

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

# Bash error expansion ${v:?msg} and ${v?msg} on a present value, checked
# byte-for-byte against bash. A present value passes through without triggering
# the error path, so the output is deterministic.
val=present
echo "${val:?should not error}"
echo "${val?also fine}"
nonempty=x
echo "${nonempty:?msg}"

v=hello
echo "${v~}"
echo "${v~~}"
w=WORLD
echo "${w~}"
echo "${w~~}"
m=MixedCase
echo "${m~~}"
echo "${m~}"
n=123abc
echo "${n~~}"

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

# Bash pattern-replacement expansion ${v/pat/rep} and its variants, checked
# byte-for-byte against bash. Covers first and all, start and end anchors, glob
# patterns, deletion, a pattern from a variable, and an escaped slash.

v=hello
echo "${v/l/L}"
echo "${v//l/L}"
echo "${v/#he/HE}"
echo "${v/%lo/LO}"
echo "${v/l}"
echo "${v/x/y}"

csv=a,b,c,d
echo "${csv//,/ }"
echo "${csv/,/;}"

cls=hello
echo "${cls//[lo]/_}"

rep=aaa
echo "${rep//a/bb}"
echo "${rep//a/}"

p=l
echo "${v//$p/L}"

path=a/b/c
echo "${path//\//_}"

greedy=axbxc
echo "${greedy/x*x/Y}"

word=Hello
echo "${word/#H/J}"
echo "${word/%o/0}"
echo "${word/#x/J}"
echo "${word/%x/0}"

amp=hello
echo "${amp/l/[&]}"
echo "${amp//l/<&>}"
echo "${amp/e/\&}"
dotted=a.b.c
echo "${dotted//./[&]}"

# Bash length expansion ${#v}, checked byte-for-byte against bash. Covers a set
# value, an empty value, an unset value, an array element count, and individual
# element lengths.
v=hello
echo "${#v}"
empty=
echo "${#empty}"
unset u
echo "${#u}"
long=abcdefghij
echo "${#long}"
arr=(one two three)
echo "${#arr[@]}"
echo "${#arr[0]}"
echo "${#arr[2]}"
spaces="a b c"
echo "${#spaces}"

# Indirect expansion carries its modifier to the target, positional slices
# yield fields, the [*] join uses the first IFS byte, and a quoted slash stays
# inside a replacement pattern.
x=0; v=x
echo "[${!v+set}]" "[${!v-fb}]"
unset x
echo "[${!v+set}]" "[${!v:-empty}]"
f() { printf "[%s]" "${@:2}"; echo; printf "[%s]" "${@: -1}"; echo; echo "[${*:2}]"; }
f a "b b" c
g() { local -a arr=(); arr+=("${@:2}"); echo "n=${#arr[@]} first=${arr[0]}"; }
g x y z
lines=(l1 l2 l3)
IFS=$'\n'
joined="${lines[*]}"
printf '%s' "$joined" | od -An -c | tr -s ' '
unset IFS
def="g/h:i () X"
echo "[${def/#"g/h:i"/"copy:g"}]"
p="a/b/c"; echo "[${p//"/"/_}]"

# The positional parameters take the ${@:-word} default, the ${@:+word}
# alternate, and the ${@:=word} pass-through the way a scalar does, distinct
# from the ${@:offset} slice. A colon test treats an empty single parameter as
# null, while two parameters stay non-null even when empty. The ${@: -1} slice
# with a leading space and the ${@:1:2} length form still run as slices. The
# fatal ${@:?} and the null ${@:=} are fenced in subshells so the run goes on.
set --
printf '<%s>' "${@:-DEF}"; echo
set -- ""
printf '<%s>' "${@:-DEF}"; echo
set -- "" ""
printf '<%s>' "${@:-DEF}"; echo
set -- a b
printf '<%s>' "${@:-DEF}"; echo
set --
printf '<%s>' ${@:-a b c}; echo
set --
printf '<%s>' "${*:-DEF}"; echo
set -- a b
printf '<%s>' "${*:-DEF}"; echo
set --
printf '<%s>' "${@:+ALT}"; echo
set -- ""
printf '<%s>' "${@:+ALT}"; echo
set -- "" ""
printf '<%s>' "${@:+ALT}"; echo
set -- a b
printf '<%s>' "${@:+ALT}"; echo
set -- a b
printf '<%s>' ${@:+one two}; echo
set --
printf '<%s>' "${@-DEF}"; echo
set -- a b
printf '<%s>' "${@-DEF}"; echo
set -- a b
printf '<%s>' "${@:=x}"; echo
set -- a b c
echo "${@: -1}"
echo "${@:1:2}"
set -- a b c d
echo "${@:2}"
set -- a b
echo "have=${@:?should not fire}"

# A value-transform modifier maps over the positional parameters, the same way
# it maps over an array, checked byte-for-byte against bash. The case mods, the
# prefix and suffix removals, and the pattern replacement each run per element.
# A quoted star joins the transformed elements under the first IFS byte, while
# the at form keeps them as separate words.
set -- foo bar baz
echo "caret: ${@^}"
echo "caretcaret: ${@^^}"
echo "comma: ${@,,}"
echo "remove_prefix: ${@#b}"
echo "remove_suffix: ${@%z}"
echo "replace: ${@/a/X}"
echo "replace_all: ${@//a/X}"
echo "star_caret: ${*^^}"
IFS=-
echo "star_join: ${*^}"
unset IFS
count=0
for word in "${@^}"; do count=$((count + 1)); echo "elem$count=$word"; done
