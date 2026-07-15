#!/bin/bash
# Bash select loop, checked byte-for-byte against bash. The menu and prompt go to
# standard error, so a piped choice leaves only the body output on stdout. Covers
# a valid pick, an out-of-range pick, a non-number pick, iteration, and an empty
# line that just reprompts.
printf '1\n' | { select x in apple banana cherry; do echo "you picked $x"; break; done; }
printf '2\n' | { select c in red green blue; do echo "color is $c"; break; done; }
printf '99\n' | { select x in a b; do echo "out of range gives [$x]"; break; done; }
printf 'notnum\n' | { select x in a b; do echo "reply=$REPLY name=[$x]"; break; done; }
printf '1\n2\n3\n' | { select item in one two three; do echo "item: $item"; done; }
printf '\n3\n' | { select x in p q r; do echo "after empty: $x"; break; done; }
printf '' | { select x in p q; do :; done; }; echo "eof-status=$?"
