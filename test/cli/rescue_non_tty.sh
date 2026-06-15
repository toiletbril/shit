unset SHIT_FLAGS
# A bad flag left in SHIT_FLAGS exits with the usage status when standard input
# is not a terminal, so a script or a pipe sees the error and the non-zero
# status. The rescue prompt is reserved for an interactive terminal, so a
# non-tty run never prints the rescue banner.
echo "== non-tty bad flag exits 2:"
SHIT_FLAGS='--nonexistent-flag' "$BIN" -c 'echo unreached' </dev/null
echo "exit=$?"
echo "== non-tty run does not enter rescue:"
SHIT_FLAGS='--nonexistent-flag' "$BIN" -c 'echo unreached' </dev/null 2>&1 |
  grep -c "Entering rescue"
