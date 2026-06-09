#!/bin/bash
# Bash [[ ]] numeric comparison operators, checked byte-for-byte against bash.
# Covers the full set -eq -ne -lt -le -gt -ge and a combination that mixes a
# numeric test with a string test under && and ||.
[[ 5 -eq 5 ]] && echo eq
[[ 5 -ne 6 ]] && echo ne
[[ 3 -lt 4 ]] && echo lt
[[ 4 -le 4 ]] && echo le
[[ 7 -gt 2 ]] && echo gt
[[ 7 -ge 7 ]] && echo ge
[[ 5 -le 4 ]] || echo not-le
[[ 9 -ge 10 ]] || echo not-ge
[[ 0 -eq 0 ]] && [[ -1 -lt 0 ]] && echo zero-and-neg
[[ abc == abc && 1 -eq 1 ]] && echo combine-and
[[ x == y || 2 -gt 1 ]] && echo combine-or
[[ ( 1 -eq 1 || 2 -eq 3 ) && abc == ab? ]] && echo paren-combine
