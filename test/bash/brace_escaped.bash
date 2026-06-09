#!/bin/bash
# Escaped and quoted braces that stay literal, and an escaped comma inside a
# group, checked byte-for-byte against bash. A backslash before a brace or a
# comma removes its special meaning, while a quote keeps the whole word literal.
echo \{a,b}
echo \{a,b\}
echo {a\,b,c}
echo {a,b\,c}
echo a{b,c\}
echo "{a,b}"
echo '{a,b}'
echo a{}b
echo a{,}b
