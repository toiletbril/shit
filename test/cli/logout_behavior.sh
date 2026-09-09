unset KOSH_FLAGS
# logout in a non-login shell prints a not-login-shell message and returns
# one, since exit is the non-login counterpart. A non-numeric operand is a
# located error pointing at the operand.
echo "== logout in a non-login shell is refused:"
"$BIN" -c 'logout' 2>&1; echo "rc=$?"
echo "== logout with a non-numeric operand is a located error:"
"$BIN" -c 'logout abc' 2>&1; echo "rc=$?"
# logout in a login shell runs the EXIT action before the shell ends. The
# operand supplies the status, a bare logout takes the status the action found,
# an external last command leaves the shell alive to carry it, and an exit the
# action runs replaces it.
echo "== logout runs the EXIT action with its operand:"
"$BIN" -l --no-init-files -c 'trap "echo action-ran" EXIT; echo body; logout 5' \
  2>&1; echo "rc=$?"
echo "== bare logout keeps the status the action found:"
"$BIN" -l --no-init-files -c 'trap "echo action-ran" EXIT; logout' 2>&1
echo "rc=$?"
echo "== an external last command in the action keeps the logout status:"
"$BIN" -l --no-init-files -c 'trap "/bin/echo external-last" EXIT; logout 6' \
  2>&1; echo "rc=$?"
echo "== an exit inside the action replaces the logout status:"
"$BIN" -l --no-init-files -c 'trap "echo head; exit 9" EXIT; logout 7' 2>&1
echo "rc=$?"
