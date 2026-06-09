#!/bin/sh
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
