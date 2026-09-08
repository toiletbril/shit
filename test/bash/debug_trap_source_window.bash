#!/bin/bash
source_window_frame() {
  echo frame-alpha-padding-padding-padding-padding-padding
  echo frame-beta-padding-padding-padding-padding-padding
  . "$1"
  echo frame-gamma-padding-padding-padding-padding-padding
  echo frame-delta-padding-padding-padding-padding-padding
}

# A command of a sourced file is published from the file that holds it. The
# padded body above covers the offsets the sourced commands are written at, and
# the running function window must not answer for them.

echo source-window
trap 'printf "D-[%s] LINENO=%s\n" "$BASH_COMMAND" "$LINENO"' DEBUG
set -T
source_window_frame "${BASH_SOURCE[0]%/*}/goldens/debug_trap_source_window_inner.bash"
trap - DEBUG
set +T

echo done
