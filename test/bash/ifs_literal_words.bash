#!/bin/bash
# Field splitting applies only to the results of expansions. Literal text
# from the source stays one word even when it holds IFS bytes, and a literal
# glued to an expansion splits only inside the expanded part.
IFS=-
echo a-b c-d
set -- a-b --
echo "n=$#"
x=p-q
printf '[%s]\n' $x a-b
printf '[%s]\n' lead-$x-tail
IFS=' '
echo a-b done
