unset SHIT_FLAGS
# calc reports a located error with a caret under the offending token rather than
# a flat message, and with -i it reads expressions interactively, evaluating each
# piped line and continuing past a bad one.
echo "== a located error carets the bad token:"
"$BIN" -c 'shitbox calc 2312312 / 1323.0' 2>&1
echo "== a valid expression prints the value:"
"$BIN" -c "shitbox calc '2 + 3 * 4'" 2>&1
echo "== 128-bit width still prints in full:"
"$BIN" -c "shitbox calc '2 ** 70'" 2>&1
echo "== -i evaluates each piped line, continuing past a bad one:"
printf '1 + 1\nbad +\n10 * 10\n' | "$BIN" -c 'shitbox calc -i' 2>&1
