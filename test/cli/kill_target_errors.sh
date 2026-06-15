unset SHIT_FLAGS
# kill rejects a missing target, an unknown signal name, an unknown job, and a
# non-numeric target rather than falling through to a process-group signal. No
# real signal is delivered, every case is an error path with a fixed message.
echo "== no target is required:"; "$BIN" -c 'kill'; echo "rc=$?"
echo "== an unknown signal name is rejected:"; "$BIN" -c 'kill -BOGUSSIG 123'; echo "rc=$?"
echo "== an unknown job is reported:"; "$BIN" -c 'kill %999'; echo "rc=$?"
echo "== a non-numeric job is rejected:"; "$BIN" -c 'kill %abc'; echo "rc=$?"
echo "== a non-numeric pid is rejected:"; "$BIN" -c 'kill notanumber'; echo "rc=$?"
