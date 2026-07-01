# The -n count returns success when the requested bytes are read and failure
# when the input ends first, and either way the field holds what was read.
unset SHIT_FLAGS
printf 'hi' | "$BIN" -c 'read -r -n 2 x; echo "rc=$? x=[$x]"'
printf 'h' | "$BIN" -c 'read -r -n 2 x; echo "rc=$? x=[$x]"'
printf '' | "$BIN" -c 'read -r -n 2 x; echo "rc=$? x=[$x]"'
