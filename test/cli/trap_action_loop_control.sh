unset KOSH_FLAGS
# A break or a continue a DEBUG action requests names the innermost enclosing
# loop. The traced command still runs, and every later command in the body is
# skipped until the loop takes the jump.
echo "== a break on a word loop header ends the loop with its variable bound:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in for\ v*) break;; esac'"'"' DEBUG
for v in 1 2 3; do echo body-$v; done
trap - DEBUG
echo v=${v-unset}'; echo "rc=$?"
echo "== a break on a body command runs that command and ends the loop:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in echo\ body*) break;; esac'"'"' DEBUG
for v in 1 2 3; do echo body-$v; echo after-$v; done
trap - DEBUG
echo tail'; echo "rc=$?"
echo "== a continue on a body command skips the rest of that iteration:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in echo\ body*) continue;; esac'"'"' DEBUG
for v in 1 2 3; do echo body-$v; echo after-$v; done
trap - DEBUG
echo tail'; echo "rc=$?"
echo "== a continue on a word loop header skips every body:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in for\ v*) continue;; esac'"'"' DEBUG
for v in 1 2 3; do echo body-$v; done
trap - DEBUG
echo v=${v-unset}'; echo "rc=$?"
echo "== an inner header continue leaves the outer body running:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in for\ v*) continue;; esac'"'"' DEBUG
for o in A B; do echo outer-$o; for v in 1 2; do echo body-$v; done; echo after-inner; done
trap - DEBUG
echo tail'; echo "rc=$?"
# A jump of two levels passes the inner loop and is taken by the outer one, so
# the two sections below print different lines.
echo "== an inner header break of two levels ends the outer loop:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in for\ v*) break 2;; esac'"'"' DEBUG
for o in A B; do echo outer-$o; for v in 1 2; do echo body-$v; done; echo after-inner; done
trap - DEBUG
echo tail'; echo "rc=$?"
echo "== an inner header continue of two levels starts the next outer round:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in for\ v*) continue 2;; esac'"'"' DEBUG
for o in A B; do echo outer-$o; for v in 1 2; do echo body-$v; done; echo after-inner; done
trap - DEBUG
echo tail'; echo "rc=$?"
echo "== a break on a while condition ends the loop:"
"$BIN" --mood bash -c 'set -T
n=0
trap '"'"'case "$BASH_COMMAND" in *-lt\ 3*) break;; esac'"'"' DEBUG
while [ $n -lt 3 ]; do echo w-$n; n=$((n + 1)); done
trap - DEBUG
echo n=$n'; echo "rc=$?"
echo "== a break on an arithmetic init clause runs the clause and ends the loop:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in *i\ =\ 0*) break;; esac'"'"' DEBUG
for ((i = 0; i < 3; i++)); do echo body-$i; done
trap - DEBUG
echo i=${i-unset}'; echo "rc=$?"
echo "== a break on a case header ends the loop before the case runs:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in case*) break;; esac'"'"' DEBUG
for o in A B; do case x in x) echo matched;; esac; echo after-case; done
trap - DEBUG
echo tail'; echo "rc=$?"
echo "== an exit in an action abandons the traced command:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in echo\ body*) exit 7;; esac'"'"' DEBUG
for v in 1 2 3; do echo body-$v; done
echo unreached'; echo "rc=$?"
