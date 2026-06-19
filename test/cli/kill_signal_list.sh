unset SHIT_FLAGS
# The kill builtin and the killall and pkill utilities list the signal names
# under -l and --list, all through one shared formatter that reads the platform
# signal table.
echo "== kill -l lists SIGTERM (count):"
"$BIN" -c 'kill -l' 2>&1 | grep -c ') SIGTERM'
echo "== kill --list lists SIGKILL (count):"
"$BIN" -c 'kill --list' 2>&1 | grep -c ') SIGKILL'
echo "== killall -l lists SIGHUP (count):"
"$BIN" -c 'shitbox killall -l' </dev/null 2>&1 | grep -c ') SIGHUP'
echo "== pkill -l lists SIGINT (count):"
"$BIN" -c 'shitbox pkill -l' </dev/null 2>&1 | grep -c ') SIGINT'
echo "== the list is numbered name lines (count of '%d) SIG'):"
"$BIN" -c 'kill -l' 2>&1 | grep -cE '^[0-9]+\) SIG'
