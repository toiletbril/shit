#!/bin/sh
# Parameter expansion, arithmetic, test, case, loops, and functions.

greet() {
    echo "hello, $1"
}

greet world
greet shell

x=hello
echo "len=${#x}"
echo "default=${missing:-fallback}"
echo "alt=${x:+present}"
echo "prefix=${x#he}"
echo "suffix=${x%lo}"

n=0
total=0
while [ "$n" -lt 5 ]; do
    total=$((total + n))
    n=$((n + 1))
done
echo "total=$total"

for word in alpha beta gamma; do
    case "$word" in
    a*) echo "$word starts with a" ;;
    b*) echo "$word starts with b" ;;
    *) echo "$word is other" ;;
    esac
done

if [ $((2 + 2)) -eq 4 ]; then
    echo "math works"
fi

double() {
    echo $(($1 * 2))
}
echo "double 21 = $(double 21)"


# case matching and $? propagation through &&, ||, ;, and pipelines.

for fruit in apple banana cherry plum; do
    case $fruit in
        apple|cherry) echo "$fruit is red-ish" ;;
        banana) echo "$fruit is yellow" ;;
        *) echo "$fruit is unknown" ;;
    esac
done

true && echo "and ran"
false || echo "or ran"
false && echo "should not print"
true || echo "should not print either"

true; echo "after true $?"
false; echo "after false $?"

echo hi | cat | cat
echo "pipe rc $?"

false | true
echo "pipe last true rc $?"

true | false
echo "pipe last false rc $?"

if echo seq | grep -q seq; then
    echo "grep matched"
fi

# POSIX function definitions, nested functions, return with explicit codes and $?.

greet() {
    echo "hello $1"
    return 0
}

classify() {
    if [ "$1" -gt 10 ]; then
        return 3
    fi
    return 7
}

greet world
classify 20
echo "rc $?"
classify 5
echo "rc $?"

outer_fn() {
    inner_fn() {
        echo "inner sees $1"
        return 42
    }
    inner_fn "$1"
    echo "inner rc $?"
    return 0
}

outer_fn payload
echo "outer rc $?"

# Recursive factorial computing a deterministic value.
fact() {
    if [ "$1" -le 1 ]; then
        echo 1
        return
    fi
    prev=$(fact $(($1 - 1)))
    echo $(($1 * prev))
}

echo "fact 5 = $(fact 5)"
echo "fact 6 = $(fact 6)"

# return value of last command propagates when return has no argument.
last_status() {
    false
    return
}
last_status
echo "implicit rc $?"

# while, until, for-in, C-style for, and break N / continue N over nested loops.

i=0
while [ "$i" -lt 4 ]; do
    i=$((i + 1))
    if [ "$i" -eq 2 ]; then
        continue
    fi
    echo "while $i"
done

j=0
until [ "$j" -ge 3 ]; do
    echo "until $j"
    j=$((j + 1))
done

for word in alpha beta gamma; do
    echo "for $word"
done

total=0
for n in 1 2 3 4 5; do
    if [ "$n" -eq 4 ]; then
        break
    fi
    total=$((total + n))
done
echo "total $total"

outer=0
while [ "$outer" -lt 3 ]; do
    outer=$((outer + 1))
    inner=0
    while [ "$inner" -lt 3 ]; do
        inner=$((inner + 1))
        if [ "$inner" -eq 2 ]; then
            continue 2
        fi
        echo "pair $outer $inner"
    done
    echo "after inner $outer"
done

a=0
while [ "$a" -lt 5 ]; do
    a=$((a + 1))
    b=0
    while [ "$b" -lt 5 ]; do
        b=$((b + 1))
        if [ "$a" -eq 2 ] && [ "$b" -eq 2 ]; then
            break 2
        fi
        echo "cell $a $b"
    done
done
echo "done"

# Subshell isolation versus brace-group shared state, and command/eval/: builtins.

x=outer
( x=inner; echo "in subshell $x" )
echo "after subshell $x"

y=before
{ y=after; echo "in brace $y"; }
echo "after brace $y"

# redirection applied to a compound statement.
{
    echo line one
    echo line two
} | cat

for n in 1 2 3; do
    echo "redir $n"
done > /tmp/control_subshell_brace_group.$$
cat /tmp/control_subshell_brace_group.$$
rm -f /tmp/control_subshell_brace_group.$$

# the : null builtin always succeeds.
:
echo "colon rc $?"
: ignored arguments
echo "colon args rc $?"

# eval builds and runs a command.
cmd="echo evaluated text"
eval "$cmd"

# command runs a builtin directly.
command echo "command builtin"

# subshell exit status propagates.
( exit 5 )
echo "subshell exit rc $?"

# Functions with return, nested control flow, test operators, and negation.

is_even() {
    if [ $(($1 % 2)) -eq 0 ]; then
        return 0
    fi
    return 1
}

for k in 1 2 3 4 5 6; do
    if is_even "$k"; then
        echo "$k even"
    else
        echo "$k odd"
    fi
done

if ! is_even 7; then
    echo "7 is not even"
fi

path=/usr/local/bin/program
echo "dir=${path%/*}"
echo "base=${path##*/}"

a=5
b=12
if [ "$a" -lt "$b" ] && [ "$b" -lt 100 ]; then
    echo "in range"
fi

label=""
if [ -z "$label" ]; then
    label=unnamed
fi
echo "label=$label"

sum=0
for value in 10 20 30; do
    sum=$((sum + value))
done
echo "sum=$sum average=$((sum / 3))"

case $((sum)) in
60) echo "sum is sixty" ;;
*) echo "sum is $sum" ;;
esac
