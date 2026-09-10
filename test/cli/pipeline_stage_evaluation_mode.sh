unset KOSH_FLAGS
# A compound pipeline stage carries its evaluation mode into the process that
# runs it. The mode decides whether the stage publishes one command boundary or
# the boundary of every command inside it. POSIX forks the stage and Windows
# relaunches a bootstrap child, and both transports produce the same trace.
echo "== a compound stage publishes one boundary without functrace:"
"$BIN" --no-traces --mood bash -c 'trap "echo debug=[\$BASH_COMMAND]" DEBUG
{ echo one; echo two; } | /bin/cat
for i in 1 2; do echo "i=$i"; done | /bin/cat
( echo sub ) | /bin/cat
echo tail'; echo "rc=$?"
echo "== a compound stage publishes every inner boundary under functrace:"
"$BIN" --no-traces --mood bash -c 'set -o functrace
trap "echo debug=[\$BASH_COMMAND]" DEBUG
{ echo one; echo two; } | /bin/cat
for i in 1 2; do echo "i=$i"; done | /bin/cat
( echo sub ) | /bin/cat
echo tail'; echo "rc=$?"
echo "== a nested pipeline inside a stage carries the mode further:"
"$BIN" --no-traces --mood bash -c 'set -o functrace
trap "echo debug=[\$BASH_COMMAND]" DEBUG
{ echo deep | /bin/cat; } | /bin/cat
echo tail'; echo "rc=$?"
echo "== clearing functrace between two stages narrows the later one:"
"$BIN" --no-traces --mood bash -c 'set -o functrace
trap "echo debug=[\$BASH_COMMAND]" DEBUG
{ echo staged; } | /bin/cat
set +o functrace
{ echo quiet; } | /bin/cat
echo tail'; echo "rc=$?"
echo "== a failing command inside a stage publishes its own boundary:"
"$BIN" --no-traces --mood bash -c 'set -o functrace
trap "echo debug=[\$BASH_COMMAND]" DEBUG
{ echo before; false; } | /bin/cat
echo "status=$?"'; echo "rc=$?"
