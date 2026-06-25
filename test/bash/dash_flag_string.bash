#!/bin/bash
# The $- flag string reports hashall and braceexpand on by default outside the
# posix mood, checked byte-for-byte against bash. set -f and set +f toggle the
# noglob flag in place.
echo "$-"
set -f
echo "$-"
set +f
set -u
echo "$-"
