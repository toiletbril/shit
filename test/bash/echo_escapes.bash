#!/bin/bash
echo -e 'tab\there'
echo -e 'a\nb'
echo -e 'esc\e.E\E.'
echo -ne 'no-nl'
echo
echo -E 'kept\tliteral'
echo -e 'stop\cgone'
echo 'no-e-literal\t.'
echo -e 'oct\101'
