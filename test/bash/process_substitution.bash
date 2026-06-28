#!/bin/bash
# Bash process substitution <(...) and >(...), checked byte-for-byte against
# bash. The input form runs the command on a pipe and substitutes the /dev/fd
# path the reader opens, the output form substitutes the path the writer feeds.
cat <(echo hello)
cat <(echo one) <(echo two)
wc -l < <(seq 5) | tr -d ' '
diff <(printf 'a\nb\nc\n') <(printf 'a\nx\nc\n') | grep -c '^[<>]'
echo <(true) | grep -c '^/dev/fd/'
sort <(printf '3\n1\n2\n')
echo data | tee >(cat > /tmp/shit_ps_out_$$) >/dev/null
sleep 0.2
cat /tmp/shit_ps_out_$$
rm -f /tmp/shit_ps_out_$$
