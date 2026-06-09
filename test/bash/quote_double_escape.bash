#!/bin/bash
# Bash double-quote escaping. A backslash inside double quotes is special only
# before $, `, ", \, and newline. Before any other char the backslash stays
# literal. The dollar, backtick, and quote forms escape their char, and a
# doubled backslash collapses to one.
echo "a\$b"
echo "a\`b"
echo "a\"b"
echo "a\\b"
echo "a\cb"
echo "a\tb"
echo "back\\slash\\\\end"
v=value
echo "lit \$v stays, real $v expands"
echo "path\\to\\thing"
echo "trailing backslash\\"
