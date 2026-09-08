#!/bin/bash

trap 'echo ERR' ERR

echo guarded-loop
for item in one two; do
  false && echo unreachable
done
echo "status=$?"

echo plain-loop
for item in one two; do
  false
done
echo "status=$?"

echo brace
{ false; }
echo "status=$?"

echo function
fail_function() {
  false
}
fail_function
echo "status=$?"

echo subshell
(false)
echo "status=$?"

echo conditional
[[ no = yes ]]
echo "status=$?"

echo arithmetic
((0))
echo "status=$?"

echo errtrace-function
set -E
fail_function
echo "status=$?"

echo errtrace-subshell
(false)
echo "status=$?"

echo return-plain
trap 'echo RETURN' RETURN
return_function() {
  true
}
return_function
echo return-functrace
set -T
return_function
set +T
trap - RETURN

echo lineno-plain
trap 'echo "at $LINENO"' ERR
false
echo lineno-function
lineno_function() {
  false
}
lineno_function
echo lineno-subshell
(false)
echo lineno-source
. bash/goldens/err_trap_lineno_inner.bash
trap - ERR
echo lineno-done

echo err-command-text
trap 'echo "E-[$BASH_COMMAND]"' ERR
false
command_text_function() {
  false
}
command_text_function
! true
[ 1 -eq 2 ]
(( 0 ))
[[ -n "" ]]
false | true
grep -q missing < /dev/null
trap - ERR
echo err-command-text-done

# A failing subshell reports the parenthesized command and the line the closing
# parenthesis is written on. The reprint lays out one blank inside each
# parenthesis, separates the commands of a multiple line body with a semicolon
# and a blank, and keeps the redirections written after the closing
# parenthesis.
echo err-subshell-text
trap 'echo "E-$LINENO-[$BASH_COMMAND]"' ERR
(false)
(   false   )
( echo subshell-a
  false )
( false ) > /dev/null
( true ) && ( false )
{ false; }
trap - ERR
echo err-subshell-text-done

# A subshell nested inside another carries the same layout, and a doubled
# parenthesis opens an arithmetic command that keeps the way it is written. A
# substitution whose body opens a subshell takes a blank after the dollar sign,
# because the two parentheses would otherwise read as arithmetic. Errtrace is
# cleared so that each statement raises one fire and the reprint stands alone.
echo err-nested-subshell-text
set +E
trap 'echo "E-[$BASH_COMMAND]"' ERR
( ( false ) )
( (false) )
( ( false
  ) )
( ( echo nested-a
    false ) )
( ( ( false ) ) )
( ((0)) )
( { false; } )
( echo "a  b" ; ( false ) )
( true && ( false ) )
( ( false ) > /dev/null )
false $( ( echo nested-b ) )
false $( (echo nested-c) )
false $( ((1)) )
trap - ERR
set -E
echo err-nested-subshell-text-done

# A failing pipeline reports the last simple stage it holds, because that is the
# stage the parent published before it forked. A compound last stage publishes
# nothing and leaves the stage written before it in place. Errtrace is cleared
# so that each pipeline raises one fire.
echo err-pipeline-site
set +E
trap 'echo "E-$LINENO-[$BASH_COMMAND]"' ERR
false |
  cat |
  grep -q nothing
true |
  grep -q \
    nothing
! true |
  cat
true |
  cat > /dev/null |
  grep -q nothing
true |
  {
    false
  }
true |
  while read -r line; do
    false
  done
trap - ERR
set -E
echo err-pipeline-site-done

# Bash runs a subshell whose body is one subshell in the process it already
# forked, so the inner parentheses raise no fire. A body that holds another
# command beside the parentheses, or wraps them in a brace group, keeps its own
# fire.
echo err-nested-subshell-fire
trap 'echo "E-$LINENO-[$BASH_COMMAND]"' ERR
( ( false ) )
( ( ( false ) ) )
( false )
( echo nested-body; ( false ) )
( true && ( false ) )
( { ( false ); } )
( ( true ) )
trap - ERR
echo err-nested-subshell-fire-done

# The redirections written around the inner parentheses are applied around the
# body the outer parentheses run, and the inner parentheses still raise no fire.
# A brace group written between the two raises its own.
echo err-redirected-subshell-fire
trap 'echo "E-$LINENO-[$BASH_COMMAND]"' ERR
( ( false ) < /dev/null )
( ( false ) 2>/dev/null )
( ( false ) > /dev/null )
( { ( false ) < /dev/null ; } )
( ( ( false ) < /dev/null ) )
trap - ERR
echo err-redirected-subshell-fire-done

# The trap belongs to a command only when the trap was already installed as the
# command began. A function that installs one for itself leaves its own call
# untraced. The action outlives the call, and a second call to the same function
# is traced.
echo err-installed-inside-function
installs_own_trap() {
  trap 'echo "I-[$BASH_COMMAND]-[${FUNCNAME[0]}]"' ERR
  false
  return 2
}
installs_own_trap
echo "first-status=$?"
installs_own_trap
echo "second-status=$?"
trap - ERR
echo err-installed-inside-function-done

# An action that returns leaves the function the failing command runs in, and
# the commands written after that command are not reached.
echo err-action-return
returns_from_action() {
  trap 'echo in-action; return 6' ERR
  false
  echo unreachable
  return 2
}
returns_from_action
echo "status=$?"
trap - ERR
echo err-action-return-done

# A trap the top level installs does not reach a subshell, a function, or a
# command substitution. Errtrace lifts the action into each of them.
echo err-inherited-scope
trap 'echo T-inherited' ERR
(false)
inherit_probe() { false; }
inherit_probe
echo "captured=$(false; echo subst-tail)"
set -E
(false)
inherit_probe
echo "captured=$(false; echo subst-tail)"
set +E
trap - ERR
echo err-inherited-scope-done

# A frame that installs a trap for itself is traced without errtrace, and a
# frame nested inside the installing one is traced as well.
echo err-installed-scope
( trap 'echo T-subshell' ERR; false; echo subshell-tail )
( trap 'echo T-nested' ERR; ( false ); echo nested-tail )
installs_in_substitution() { trap 'echo T-substitution' ERR; false; }
echo "captured=$(installs_in_substitution)"
echo "s=$?"
echo err-installed-scope-done

# The action a function installs outlives the call. The caller keeps it, and a
# second failure in the same frame fires it again. A function that overrides the
# inherited action leaves its own behind. A function that resets the action
# leaves the inherited one standing.
echo err-installed-outlives
sets_own_action() { trap 'echo T-set-in-function' ERR; }
sets_own_action
echo "listed=[$(trap -p ERR)]"
false
echo tail
trap - ERR
trap 'echo T-outer' ERR
overrides_action() { trap 'echo T-override' ERR; }
overrides_action
echo "listed=[$(trap -p ERR)]"
false
trap - ERR
trap 'echo T-kept' ERR
removes_action() { trap - ERR; }
removes_action
echo "listed=[$(trap -p ERR)]"
false
trap - ERR
fires_twice() {
  trap 'echo T-twice' ERR
  false
  false
  echo twice-tail
}
fires_twice
trap - ERR
echo "s=$?"
echo err-installed-outlives-done
