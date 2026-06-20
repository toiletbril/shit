# A builtin or a utility that is missing a required argument renders a located
# error followed by a note that points at the help, the same caret in every
# mood. Each case is a fixed error path that delivers no real signal and touches
# no file.
unset SHIT_FLAGS

echo "--- builtin kill with no target ---"
"$BIN" -c 'kill'; echo "rc=$?"
echo "--- builtin getopts with no arguments ---"
"$BIN" -c 'getopts' 2>&1; echo "rc=$?"
echo "--- builtin let with no expression ---"
"$BIN" -c 'let' 2>&1; echo "rc=$?"
echo "--- calc with no expression ---"
"$BIN" -c 'shitbox calc' 2>&1; echo "rc=$?"
echo "--- shitbox cp with one operand ---"
"$BIN" -c 'shitbox cp onlyone' 2>&1; echo "rc=$?"
echo "--- shitbox grep with no pattern ---"
"$BIN" -c 'shitbox grep' 2>&1; echo "rc=$?"
echo "--- the note is located in the bash mood too ---"
"$BIN" --mood bash -c 'shitbox kill' 2>&1; echo "rc=$?"
echo "--- and in the sh mood ---"
"$BIN" --mood sh -c 'shitbox mkdir' 2>&1; echo "rc=$?"
