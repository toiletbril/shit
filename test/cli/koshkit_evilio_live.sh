#!/bin/sh

live_report_path=$TEST_TEMP_DIRECTORY/evilio-live-report
set -m
"$BIN" -c \
  'koshkit evilio --live --cumulative=0.02 --ps -1 --color never' \
  > "$live_report_path" &
live_pid=$!
set +m

live_attempt=0
while [ ! -s "$live_report_path" ] && [ "$live_attempt" -lt 250 ]; do
  sleep 0.02
  live_attempt=$((live_attempt + 1))
done

if kill -0 "$live_pid" 2> "$TEST_NULL_DEVICE"; then
  kill -INT "$live_pid"
fi
wait "$live_pid"
live_status=$?

live_report=$(< "$live_report_path")
case $live_report in
  *PID*READ/S*WRITE/S*COMMAND*) live_shape=matched ;;
  *) live_shape=wrong ;;
esac
case $live_report in
  *DEVICE*|*MEMORY*|*SWAP*) live_scope=extra ;;
  *) live_scope=only-process-io ;;
esac

printf 'status=%s\n' "$live_status"
printf 'shape=%s\n' "$live_shape"
printf 'scope=%s\n' "$live_scope"
