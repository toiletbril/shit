#!/bin/bash
# The CHLD trap against a job that is stopped and continued, checked against
# bash. Job control is on, so the shell observes the stop and the continue.
# Each settle sleep is a child of its own and adds one to the count.
set -m

bump() { chld_count=$((chld_count+1)); }
trap bump CHLD

/bin/sleep 5 &
job_pid=$!
/bin/sleep 0.3

chld_count=0
kill -STOP "$job_pid"
/bin/sleep 0.3
jobs > /dev/null
echo "stop_delta=$chld_count"

chld_count=0
kill -CONT "$job_pid"
/bin/sleep 0.3
jobs > /dev/null
echo "continue_delta=$chld_count"

chld_count=0
kill "$job_pid"
wait "$job_pid" 2> /dev/null
echo "reap_delta=$chld_count"

trap - CHLD
chld_count=0
/bin/sleep 0.05 &
wait
echo "after_removal=$chld_count"
