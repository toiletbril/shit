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
