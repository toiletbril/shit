#!/bin/bash
case ${COLUMNS:-unset} in
  [0-9]*) echo "COLUMNS numeric" ;;
  *) echo "COLUMNS=${COLUMNS:-unset}" ;;
esac
