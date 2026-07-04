unset SHIT_FLAGS
# calc with -p reads and evaluates expressions from standard input one per line
# with no prompt, a name = value line binds a variable for a later line, and
# with no expression off a pipe it reports a verbose located error.
echo "== -p evaluates each piped line and binds a variable:"
printf '2 + 2\nx = 5\nx * x\n' | "$BIN" -c 'shitbox calc -p' 2>&1
echo "== --pipe is the long form:"
printf '7 * 6\n' | "$BIN" -c 'shitbox calc --pipe' 2>&1
echo "== no expression off a pipe reports a verbose error:"
printf '' | "$BIN" -c 'shitbox calc' 2>&1
