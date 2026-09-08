#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This file defines the driver status rule and the failure file rule the golden
# runners share. It is sourced from the test directory.

# Reports whether a driver status means the harness failed instead of the
# fixture. A timeout reports 124, the bounded driver reports 125, and a signal
# reports 128 or above. A missing or non-executable driver reports 126 or 127.
# A fixture can report those as well. A comparison run lets its golden decide.
# A refill run has no golden and must reject the truncated output.
is_driver_status_harness_failure()
{
  case $1 in
  124 | 125)
    return 0
    ;;
  126 | 127)
    if [ "$2" = yes ]; then
      return 0
    fi

    return 1
    ;;
  esac

  if [ "$1" -ge 128 ]; then
    return 0
  fi

  return 1
}

# Binds GOLDEN_FAILURE_FILE to the file the caller appends one fixture diff to.
# Each fixture owns its own file. Concurrent workers cannot interleave their
# diffs. The suite driver appends every such file to the shared failure list
# after the last worker has finished.
set_golden_failure_file()
{
  GOLDEN_FAILURE_DIRECTORY="$FAILED_LIST.d"
  mkdir -p "$GOLDEN_FAILURE_DIRECTORY"
  GOLDEN_FAILURE_FILE="$GOLDEN_FAILURE_DIRECTORY/$1.diff"
}
