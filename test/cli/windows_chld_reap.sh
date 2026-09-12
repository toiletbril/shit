unset KOSH_FLAGS
# Windows raises no child signal, and the reap itself reaches the next command
# boundary. The CHLD action therefore runs once for every child on both
# platforms. The Windows kill builtin sends only KILL and TERM, so a script
# there cannot raise CHLD by hand. Each section prints the same token on both
# platforms, and the capability sections assert agreement with the platform.
echo "== a reaped child reaches the next command boundary:"
"$BIN" --no-traces --mood bash -c 'trap "chld_count=\$((chld_count+1))" CHLD
chld_count=0
"$1" --no-traces -c "exit 0"
echo "reaped=$chld_count"
chld_count=0
"$1" --no-traces -c "exit 0" &
wait
echo "background=$chld_count"
chld_count=0
"$1" --no-traces -c "exit 0" > /dev/null
echo "redirected=$chld_count"' shell "$BIN"
echo "rc=$?"

echo "== the reap reaches the boundary and not the middle of a list:"
"$BIN" --no-traces --mood bash -c 'trap "echo chld-arrived" CHLD
"$1" --no-traces -c "exit 0"
echo after-the-child
trap - CHLD
"$1" --no-traces -c "exit 0"
echo after-the-removal' shell "$BIN"
echo "rc=$?"

echo "== raising CHLD by hand follows the platform:"
"$BIN" --no-traces --mood bash -c 'if [ "${OS-}" = Windows_NT ]; then
  expected=rejected
else
  expected=accepted
fi
if kill -CHLD $$ 2> /dev/null; then
  observed=accepted
else
  observed=rejected
fi
if [ "$expected" = "$observed" ]; then
  echo hand-raise-follows-the-platform
else
  echo "hand-raise-disagrees expected=$expected observed=$observed"
fi'
echo "rc=$?"

echo "== KILL and TERM reach a child on both platforms:"
"$BIN" --no-traces --mood bash -c '"$1" --no-traces -c "sleep 30" &
victim=$!
kill -TERM "$victim" 2> /dev/null
if [ $? -eq 0 ]; then
  echo term-accepted
else
  echo term-rejected
fi
wait "$victim" 2> /dev/null
term_status=$?
if [ "$term_status" -ne 0 ]; then
  echo term-ended-child
else
  echo "term-left-child status=$term_status"
fi
"$1" --no-traces -c "sleep 30" &
victim=$!
kill -KILL "$victim" 2> /dev/null
if [ $? -eq 0 ]; then
  echo kill-accepted
else
  echo kill-rejected
fi
wait "$victim" 2> /dev/null
kill_status=$?
if [ "$kill_status" -ne 0 ]; then
  echo kill-ended-child
else
  echo "kill-left-child status=$kill_status"
fi
echo both-signals-checked' shell "$BIN"
echo "rc=$?"
