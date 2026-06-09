#!/bin/sh
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
