#!/bin/bash
# Normalize the CLI invocation trace so a golden does not break when the
# binary moves between rel, dbg, or a different build tree. The trace echoes
# argv[0], and the Makefile resolves BIN to an absolute path, so the path
# leaks into the output. The path is replaced by the stable token SHIT, the
# trace column is zeroed since the caret width depends on the real path
# length, and the caret line that follows the SHIT invocation is dropped,
# the way warning_source_chain normalizes INNER and OUTER.
#
# Usage: ... | normalize-trace.sh "$BIN"
BIN=$1
sed "s|$BIN|SHIT|; s/\(shit: [0-9]*\):[0-9]*: trace location:/\1:0: trace location:/" \
  | awk '/SHIT -/{print; skip=1; next} skip{skip=0; next} {print}'
