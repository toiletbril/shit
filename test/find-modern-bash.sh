#!/bin/sh
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This file prints the path of the first GNU Bash on PATH whose version is 5.3
# or newer, which is the version the Bash compatibility suite compares against.
# A host commonly installs an older Bash under the name that is found first, and
# the Koshka shell itself is often installed under that name as well, so the
# version banner of every candidate is inspected. Nothing is printed and the
# status is nonzero when no suitable interpreter is installed.

is_modern_gnu_bash() {
  BANNER=$("$1" --version 2>/dev/null)

  case $BANNER in
  "GNU bash, version "*) ;;
  *) return 1 ;;
  esac

  VERSION_TEXT=${BANNER#"GNU bash, version "}
  VERSION_TEXT=${VERSION_TEXT%%[!0-9.]*}
  MAJOR_VERSION=${VERSION_TEXT%%.*}
  MINOR_VERSION=${VERSION_TEXT#*.}
  MINOR_VERSION=${MINOR_VERSION%%.*}

  case $MAJOR_VERSION in
  '' | *[!0-9]*) return 1 ;;
  esac

  case $MINOR_VERSION in
  '' | *[!0-9]*) return 1 ;;
  esac

  if [ "$MAJOR_VERSION" -gt 5 ]; then
    return 0
  fi

  if [ "$MAJOR_VERSION" -eq 5 ] && [ "$MINOR_VERSION" -ge 3 ]; then
    return 0
  fi

  return 1
}

IFS=:
set -f

for SEARCH_DIRECTORY in $PATH; do
  if [ -n "$SEARCH_DIRECTORY" ] && [ -x "$SEARCH_DIRECTORY/bash" ] &&
    is_modern_gnu_bash "$SEARCH_DIRECTORY/bash"
  then
    printf '%s\n' "$SEARCH_DIRECTORY/bash"
    exit 0
  fi
done

exit 1
