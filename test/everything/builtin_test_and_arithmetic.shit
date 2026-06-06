#!/bin/sh
# The test builtin and arithmetic expansion, checked against dash.

test 1 -eq 1 && echo "eq_ok"
test 2 -ne 1 && echo "ne_ok"
[ abc = abc ] && echo "streq_ok"
[ -z "" ] && echo "empty_ok"
[ -n "x" ] && echo "nonempty_ok"
[ 5 -gt 3 ] && echo "gt_ok"
[ 3 -lt 5 ] && echo "lt_ok"

echo "sum=$((2 + 3 * 4))"
x=10
echo "mul=$((x * 2))"
echo "ref=$(($x + 5))"
d=0
while [ "$d" -le 3 ]; do
    echo "count=$d"
    d=$((d + 1))
done

case hello in
    h*) echo "case_glob" ;;
    *) echo "case_default" ;;
esac
