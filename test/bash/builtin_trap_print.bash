#!/bin/bash
# trap -p prints the trap for a named condition with the SIG prefix bash uses, so
# the line reloads. The conditions are named so bash's own default job-control
# traps stay out of the comparison.
trap 'echo caught' INT
echo "p=$(trap -p INT)"
echo "bare=$(trap -p TERM)"
