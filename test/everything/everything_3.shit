#!/bin/sh
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
