unset SHIT_FLAGS
# Unknown flag errors carry the program context. A builtin reports the
# 'Builtin name' prefix and a shitbox utility reports the 'shitbox util'
# prefix, with the caret staying on the flag.
echo "== builtin unknown flag carries the Builtin prefix:"
"$BIN" -c 'enable --badflag' 2>&1
echo "== shitbox unknown flag carries the shitbox util prefix:"
"$BIN" -c 'shitbox ls --dasdas' 2>&1
