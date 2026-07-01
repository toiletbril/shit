#!/bin/bash
# The printf format is recycled over the extra arguments. A numeric conversion
# with no remaining argument substitutes zero and the status stays zero, the
# same as bash. An explicit empty argument is still an invalid number and the
# status is one.
printf '%d %d %d\n' 1 2 3 4 5
echo "recycle_status=$?"
printf '%d\n' '' 2>/dev/null
echo "empty_status=$?"
