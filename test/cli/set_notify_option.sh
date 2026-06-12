unset SHIT_FLAGS
# set -b backs a real notify option now, so the state round-trips through
# set +o and the letter form.
"$BIN" -c 'set -o | grep notify'
"$BIN" -c 'set -b; set -o | grep notify'
"$BIN" -c 'set -o notify; set +b; set -o | grep notify'
echo "rc=$?"
