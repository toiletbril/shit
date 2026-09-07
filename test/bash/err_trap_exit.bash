#!/bin/bash

set -o errtrace
trap 'echo caught; exit 9' ERR

echo subshell
( false; echo unreachable; echo also-unreachable )
echo unreached-end
