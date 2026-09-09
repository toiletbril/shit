#!/bin/sh
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This script verifies time keyword and builtin formatting, resident memory
# reporting, runtime mood changes, and explicit TIMEFORMAT values.

unset KOSH_FLAGS

# The digits of a time report vary between runs. Each line is printed up to its
# first digit, which leaves the label and the separator that follows it.
report_shape()
{
  printf '%s\n' "$1" | while IFS= read -r line; do
    printf '[%s]' "${line%%[0-9]*}"
  done
  printf '\n'
}

report=$("$BIN" --no-init-files --mood bash -c \
  'time "$1" --no-init-files -c :; set --mood kosh; time "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1)
echo "runtime-mood=$(report_shape "$report")"

report=$("$BIN" --no-init-files --no-diagnostics -c \
  'TIMEFORMAT=""; time -R "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1)
echo "kosh-rss=$(report_shape "$report")"

report=$("$BIN" --no-init-files --no-diagnostics -c \
  'TIMEFORMAT=""; time -R true' 2>&1)
echo "zero-rss=$(report_shape "$report")"

report=$("$BIN" --no-init-files --no-diagnostics -c \
  'TIMEFORMAT=""; builtin time -R "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1)
echo "builtin-rss=$(report_shape "$report")"

report=$("$BIN" --no-init-files --no-diagnostics -c \
  'time -p -R "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1)
echo "posix=$(report_shape "$report")"

report=$("$BIN" --no-init-files --mood bash -c \
  'TIMEFORMAT=custom; time -R "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1)
echo "bash-custom=$(report_shape "$report")"

report=$("$BIN" --no-init-files --mood bash -c \
  'builtin time "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1)
echo "bash-builtin=$(report_shape "$report")"
