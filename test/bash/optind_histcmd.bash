#!/bin/bash
echo "optind=[$OPTIND]"
echo "histcmd=[$HISTCMD]"
declare -p OPTIND
declare -p HISTCMD
HISTCMD=5
echo "after-histcmd=[$HISTCMD]"
declare -p HISTCMD
OPTIND=3
echo "after-optind=[$OPTIND]"
declare -p OPTIND
set -- -a -b operand
OPTIND=1
while getopts ab option; do
  echo "option=$option optind=$OPTIND"
done
echo "final-optind=[$OPTIND]"
shift $((OPTIND - 1))
echo "remaining=[$*]"
