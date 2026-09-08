#!/bin/bash
# The body of a sourced file is a frame of its own. The DEBUG trap of the caller
# reaches its commands only while the functrace option is set.

INNER="${BASH_SOURCE[0]%/*}/goldens/debug_trap_sourced_commands_inner.bash"

echo == top level untraced ==
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
. "$INNER"
trap - DEBUG

echo == top level traced ==
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
set -T
. "$INNER"
set +T
trap - DEBUG

sourcing_frame() {
  . "$INNER"
}

echo == function untraced ==
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
sourcing_frame
trap - DEBUG

echo == function traced ==
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
set -T
sourcing_frame
set +T
trap - DEBUG

echo == trap installed inside the sourced file ==
sourced_installer() {
  trap 'echo "I-[$BASH_COMMAND]"' DEBUG
  . "$INNER"
  trap - DEBUG
}
sourced_installer
echo after-installer

echo done
