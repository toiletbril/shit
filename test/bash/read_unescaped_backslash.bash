#!/bin/bash
# Without -r the read builtin processes backslashes, checked byte-for-byte
# against bash. A trailing backslash continues the line into the next, a
# backslash before a space escapes the field separator so the space stays in the
# word, a backslash before a plain byte is dropped, and a doubled backslash
# yields one literal backslash.
printf 'a\\\nb c\n' | { read line; echo "[$line]"; }
printf 'first \\\nsecond\n' | { read a b c; echo "a=[$a] b=[$b] c=[$c]"; }
printf 'one\\ two three\n' | { read a b; echo "a=[$a] b=[$b]"; }
printf 'x\\ty\n' | { read a b; echo "a=[$a] b=[$b]"; }
printf 'keep\\\\literal\n' | { read x; echo "x=[$x]"; }
printf 'raw\\ kept\n' | { read -r a b; echo "raw a=[$a] b=[$b]"; }
