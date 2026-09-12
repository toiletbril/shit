# shellcheck disable=SC2154
unset KOSH_FLAGS
# Under the bash mood, declare -f reprints a stored function body the way bash
# lays it out. A trailing space is rendered as <SP> so the golden holds none.
# Each section keeps the status of the shell in a variable before the filter
# runs, because the status of a pipeline belongs to its last stage.

work=$(mktemp -d)
trap '[ -n "$work" ] && /bin/rm -rf "$work"' EXIT

# Vertical compounds. An elif chain expands into a nested if, a brace group
# opens on its own line, a word loop puts do on its own line, and a case arm
# indents its body one step past its pattern.
"$BIN" --no-traces --mood bash -c 'p () { if a; then b; elif c; then d; elif e; then f; else g; fi; }
q () { echo one; { echo grp; }; echo two; }
r () { echo a
echo b
}
s () { for i in 1 2; do echo "$i"; done; echo tail; }
t () { case $x in a) echo A;; esac; echo after; }
declare -f' > "$work/out"
status=$?
sed 's/ $/<SP>/' "$work/out"
echo "rc=$status"

# Horizontal constructs. A subshell keeps its statements at the opening indent,
# a pipeline stays on one line, a while loop joins do to its condition, and a
# brace group inside an and-or chain opens on the chain line.
"$BIN" --no-traces --mood bash -c 'u () { ( echo a; echo b ); echo c; }
v () { echo a | /bin/cat | /usr/bin/wc -l; }
w () { if a; then ( b; c ); fi; }
x () { while read -r l; do echo "$l"; done < /dev/null; }
y () { a && { b; c; } || d; }
z () { local v="a b"; echo "${v}" $((1+2)) `echo t`; }
declare -f' > "$work/out"
status=$?
sed 's/ $/<SP>/' "$work/out"
echo "rc=$status"

# Redirections. An operand is spaced away from its operator, a descriptor
# duplication keeps its operand attached and gains the descriptor it defaults
# to, and a close is always written with the output form.
"$BIN" --no-traces --mood bash -c 'r1 () { echo x >&2; }
r2 () { read v <&3; }
r3 () { echo x >&-; }
r4 () { echo x <&-; }
r5 () { echo x >&file; }
r6 () { echo x 1>&2; }
r7 () { exec 3<&0; }
r8 () { exec 3>&-; }
r9 () { echo hi >/dev/null 2>&1; }
ra () { echo x &>file; }
rb () { echo x &>>file; }
rc () { echo x >>log; }
rd () { read -r v <<<"$q"; }
re () { cat <(echo p); }
declare -f' > "$work/out"
status=$?
sed 's/ $/<SP>/' "$work/out"
echo "rc=$status"

# Here documents. A body and its delimiter follow the opening line unindented,
# a blank line closes the document, a quoted delimiter is reprinted in single
# quotes, and the dash form strips the leading tabs of every body line.
"$BIN" --no-traces --mood bash -c 'h1 () { cat <<EOF
body
EOF
echo tail
}
h2 () { cat <<"E1"
$x
E1
}
h3 () { cat <<\E2
$x
E2
}
h4 () { cat <<-E3
	tabbed
	E3
echo after
}
declare -f' > "$work/out"
status=$?
sed 's/ $/<SP>/' "$work/out"
echo "rc=$status"
