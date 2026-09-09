#!/bin/bash

# An open of a named pipe blocks until a writer arrives. A trapped signal that
# lands while the open blocks runs its action and the open resumes, so the
# redirection still reaches the file and the read succeeds. A trapped interrupt
# behaves the same way. The whole file is skipped where named pipes are
# unavailable, and both shells take that branch together.

dir=$(mktemp -d)
if [ ! -d "$dir" ]; then
  echo could-not-make-a-directory
  exit 1
fi
trap 'rm -rf "$dir"' EXIT

if ! mkfifo "$dir/first" 2> /dev/null; then
  echo named-pipes-are-unavailable
  echo trap-during-blocking-open-done
  exit 0
fi

echo open-under-a-trapped-signal
trap 'echo action-signal' USR1
( /bin/sleep 1; kill -USR1 $$ ) &
notifier=$!
( /bin/sleep 2; echo signal-payload > "$dir/first" ) &
writer=$!
read -r line < "$dir/first"
echo "signal-status=$? signal-line=$line"
wait "$notifier" 2> /dev/null
wait "$writer" 2> /dev/null
trap - USR1
echo open-under-a-trapped-signal-done

echo open-under-a-trapped-interrupt
mkfifo "$dir/second"
trap 'echo action-interrupt' INT
( /bin/sleep 1; kill -INT $$ ) &
notifier=$!
( /bin/sleep 2; echo interrupt-payload > "$dir/second" ) &
writer=$!
read -r line < "$dir/second"
echo "interrupt-status=$? interrupt-line=$line"
wait "$notifier" 2> /dev/null
wait "$writer" 2> /dev/null
trap - INT
echo open-under-a-trapped-interrupt-done

echo open-that-no-signal-reaches
mkfifo "$dir/third"
( /bin/sleep 1; echo quiet-payload > "$dir/third" ) &
writer=$!
read -r line < "$dir/third"
echo "quiet-status=$? quiet-line=$line"
wait "$writer" 2> /dev/null
echo open-that-no-signal-reaches-done

echo trap-during-blocking-open-done
