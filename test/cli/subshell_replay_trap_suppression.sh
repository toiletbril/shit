unset KOSH_FLAGS
# A forked child inherits the shell state of its parent. Windows rebuilds that
# state by replaying shell source inside a bootstrap child, and no trap may
# observe a replayed command. POSIX forks and replays nothing, so both platforms
# publish only the commands the child was asked to run.
echo "== a subshell publishes its own commands and none of the replay:"
"$BIN" --no-traces --mood bash -c 'set -o functrace -o errtrace
alias greet="echo hi"
myfn() { echo fn; }
export EV=1
trap "echo debug=[\$BASH_COMMAND]" DEBUG
trap "echo err=[\$BASH_COMMAND]" ERR
( echo inner; myfn; false )
echo tail'; echo "rc=$?"
echo "== a large inherited state stays invisible to the traps:"
"$BIN" --no-traces --mood bash -c 'set -o functrace
arr=(a b c)
declare -A map=([k]=v)
readonly frozen=cold
shopt -s extglob
umask 0022
trap "echo debug=[\$BASH_COMMAND]" DEBUG
( echo "arr=${arr[1]}"; echo "map=${map[k]}"; echo "frozen=$frozen" )
echo tail'; echo "rc=$?"
echo "== a nested subshell replays twice and publishes once:"
"$BIN" --no-traces --mood bash -c 'set -o functrace
deep() { echo deep; }
trap "echo debug=[\$BASH_COMMAND]" DEBUG
( ( deep ) )
echo tail'; echo "rc=$?"
echo "== a pipeline stage and a substitution suppress the replay too:"
"$BIN" --no-traces --mood bash -c 'set -o functrace
helper() { echo helper; }
trap "echo debug=[\$BASH_COMMAND]" DEBUG
{ helper; } | /bin/cat
captured=$(helper)
echo "captured=$captured"
echo tail'; echo "rc=$?"
echo "== the trap survives the fork and still fires in the parent:"
"$BIN" --no-traces --mood bash -c 'set -o functrace
trap "echo debug=[\$BASH_COMMAND]" DEBUG
( echo child )
trap -p DEBUG
echo tail'; echo "rc=$?"
