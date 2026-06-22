unset SHIT_FLAGS
# disown -r drops only the running jobs and keeps a stopped one. The recorded
# state is refreshed first, so a job stopped since the last poll is kept rather
# than dropped, even with no jobs call before the disown.
echo "== one stopped job survives disown -r:"
"$BIN" --mood bash -c 'sleep 5 & q=$!; sleep 5 & p=$!; kill -STOP "$p"; disown -r; jobs; kill "$q" 2>/dev/null; kill -CONT "$p" 2>/dev/null; kill "$p" 2>/dev/null' 2>&1 | grep -c Stopped
echo "== and no running job remains after disown -r:"
"$BIN" --mood bash -c 'sleep 5 & q=$!; sleep 5 & p=$!; kill -STOP "$p"; disown -r; jobs; kill "$q" 2>/dev/null; kill -CONT "$p" 2>/dev/null; kill "$p" 2>/dev/null' 2>&1 | grep -c Running
