#!/bin/bash

# The digits of a time report vary between runs. Every digit is printed as N,
# which keeps the labels, the separators, and the width of each number.
report_shape() {
  line_count=0
  while IFS= read -r line; do
    line_count=$((line_count + 1))
    printf '%d [%s]\n' "$line_count" "${line//[0-9]/N}"
  done < "$1"
  printf 'lines %d\n' "$line_count"
}

work_directory=$(mktemp -d)
capture=$work_directory/report

echo "== default layout =="
{ time sleep 0; } 2>"$capture"
report_shape "$capture"

echo "== posix layout =="
{ time -p sleep 0; } 2>"$capture"
report_shape "$capture"

echo "== time format =="
TIMEFORMAT='fixed report'
{ time sleep 0; } 2>"$capture"
report_shape "$capture"

echo "== posix ignores the time format =="
{ time -p sleep 0; } 2>"$capture"
report_shape "$capture"

echo "== empty time format =="
TIMEFORMAT=
{ time sleep 0; } 2>"$capture"
report_shape "$capture"

echo "== conversions =="
TIMEFORMAT='%%R=%0R U=%0U S=%0S'
{ time sleep 0; } 2>"$capture"
report_shape "$capture"

echo "== minutes conversion =="
TIMEFORMAT='%0lR'
{ time sleep 0; } 2>"$capture"
report_shape "$capture"
unset TIMEFORMAT

echo "== status of the timed command =="
{ time false; } 2>"$capture"
printf 'status %d\n' "$?"
report_shape "$capture"
