#!/bin/bash
shopt -qo posix; echo "posix=$?"
set -e; shopt -qo errexit; echo "e=$?"
set +e; shopt -qo errexit; echo "e2=$?"
shopt -qo nounset; echo "u=$?"
set -u; shopt -qo nounset; echo "u2=$?"
shopt -qs progcomp; echo "shopt-still-works=$?"
