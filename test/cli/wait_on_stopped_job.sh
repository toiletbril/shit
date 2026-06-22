unset SHIT_FLAGS
"$BIN" --mood bash -c 'sleep 5 & p=$!; kill -STOP "$p"; wait %1; echo WAIT_RETURNED; kill -CONT "$p" 2>/dev/null; kill "$p" 2>/dev/null' 2>&1 | grep -c WAIT_RETURNED
