#!/bin/bash
# bash fails the command and goes on after a readonly assignment and an
# expansion error, while a set -u read and a ${name:?} abort the run, here
# fenced in subshells so the statuses surface, the behaviors case.tests and
# varenv exercise.
readonly xx=1
case 1 in $((xx++)) ) echo hi1 ;; *) echo hi2; esac
echo "case_went_on=$?"
(xx=2) 2>/dev/null
echo "readonly_assign=$?"
(set -u; echo "$not_set_at_all"; echo not_reached) 2>/dev/null
echo "nounset_abort=$?"
(echo "${also_missing:?gone}"; echo not_reached) 2>/dev/null
echo "report_abort=$?"
echo survived
