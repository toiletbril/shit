unset SHIT_FLAGS
"$BIN" --mood bash -c 'sleep 5 & p=$!; kill -STOP "$p"; jobs >/dev/null; wait %1; echo WAIT_RETURNED; kill -CONT "$p" 2>/dev/null; kill "$p" 2>/dev/null' 2>&1 | grep -c WAIT_RETURNED
"$BIN" --mood bash -c 'yes | sh -c '\''kill -STOP "$$"; sleep 30'\'' & wait %1; echo WAIT_RETURNED; kill -CONT %1 2>/dev/null; kill %1 2>/dev/null' 2>&1 | grep -c WAIT_RETURNED
