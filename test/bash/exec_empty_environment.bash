#!/bin/bash
# exec -c runs the command with an empty environment, so env prints nothing. A
# plain exec keeps the environment, so an exported marker still shows. Each exec
# runs inside a command substitution or a pipeline stage, so it replaces the
# contained child and the script survives to print both cases.
export MARKER=present
echo "cleared=[$(exec -c env)]"
echo "kept=$(exec env | grep '^MARKER=')"
echo "alive=yes"
