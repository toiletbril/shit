# The remaining utilities, sleep with a zero and a bad duration, env applying an
# assignment and running a command, and the pkill and killall error paths. The
# process matchers use a name that matches nothing, so no real process is
# signalled.
unset SHIT_FLAGS

echo "--- sleep zero ---"
"$BIN" -c 'shitbox sleep 0'
echo "rc=$?"

echo "--- sleep bad duration ---"
"$BIN" -c 'shitbox sleep abc' 2>&1
echo "rc=$?"

echo "--- env runs the command ---"
"$BIN" -c 'shitbox env X=1 shitbox echo ran'
echo "rc=$?"

echo "--- env applies the assignment ---"
"$BIN" -c 'shitbox env SHITBOX_TESTVAR=present | shitbox grep SHITBOX_TESTVAR'

echo "--- pkill with no pattern ---"
"$BIN" -c 'shitbox pkill' 2>&1
echo "rc=$?"

echo "--- pkill with no match ---"
"$BIN" -c 'shitbox pkill no_such_process_xyz_123'
echo "rc=$?"

echo "--- killall with no match ---"
"$BIN" -c 'shitbox killall no_such_process_xyz_123' 2>&1
echo "rc=$?"

echo "--- kill with no pid ---"
"$BIN" -c 'shitbox kill' 2>&1
echo "rc=$?"

echo "--- kill with a non-numeric pid ---"
"$BIN" -c 'shitbox kill notapid' 2>&1
echo "rc=$?"

echo "--- ps prints the header ---"
"$BIN" -c 'shitbox ps | shitbox head -n 1'
echo "rc=$?"

echo "--- list prints the utility count ---"
"$BIN" -c 'shitbox --list | shitbox wc -l'
