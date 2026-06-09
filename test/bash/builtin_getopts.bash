#!/bin/bash
# Bash getopts in silent mode, checked byte-for-byte against bash. The leading
# colon selects silent error handling, so a bad option and a missing argument
# arrive through the question-mark and colon cases with OPTARG set, while OPTIND
# advances across grouped and separate options.
parse() {
  local opt OPTIND=1
  while getopts ":ab:c" opt; do
    case $opt in
      a) echo "flag a";;
      b) echo "b arg=$OPTARG";;
      c) echo "flag c";;
      \?) echo "bad opt: $OPTARG";;
      :) echo "missing arg: $OPTARG";;
    esac
  done
  echo "OPTIND=$OPTIND"
}
parse -a -b val -c
parse -ab val
parse -x
parse -b
parse -a foo
