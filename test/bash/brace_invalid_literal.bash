#!/bin/bash
# Brace forms that bash leaves literal because the endpoints do not form a valid
# range, a number against a letter or a non-integer endpoint, checked
# byte-for-byte against bash. A single mixed list still expands.
echo {1,a}
echo {1..a}
echo {a..1}
echo {.5..5}
echo {1.5..5}
echo {a,b,c,}
echo {a,b,}
echo {,a,b}
