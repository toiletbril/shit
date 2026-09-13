unset KOSH_FLAGS

BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

capture_help()
{
  command_text=$1
  if script -q -c true /dev/null >/dev/null 2>&1; then
    NO_COLOR= TERM=xterm script -q -c "$command_text" /dev/null 2>/dev/null |
      tr -d '\r'
  elif script -q /dev/null /usr/bin/true >/dev/null 2>&1; then
    NO_COLOR= TERM=xterm script -q /dev/null /bin/sh -c "$command_text" \
      2>/dev/null | tr -d '\r'
  else
    return 1
  fi
}

check_contains()
{
  output=$1
  expected=$2
  name=$3
  case "$output" in
    *"$expected"*) echo "$name=passed" ;;
    *) echo "$name=failed" ;;
  esac
}

check_excludes()
{
  output=$1
  rejected=$2
  name=$3
  case "$output" in
    *"$rejected"*) echo "$name=failed" ;;
    *) echo "$name=passed" ;;
  esac
}

escape=$(printf '\033')
main_help=$(capture_help "exec \"$BIN\" --help") || exit 1
builtin_help=$(capture_help "exec \"$BIN\" -c 'help set'") || exit 1
koshkit_help=$(capture_help \
  "exec \"$BIN\" -c 'koshkit goodnode --help'") || exit 1

check_contains "$main_help" \
  "${escape}[1;34mSYNOPSIS${escape}[0m" main-heading
check_contains "$main_help" "${escape}[32m  $BIN" main-synopsis
check_contains "$main_help" \
  "  -M, --mood${escape}[2m=<...>${escape}[0m" main-flag
check_contains "$builtin_help" \
  "  -M, --mood${escape}[2m=<...>${escape}[0m" builtin-flag
check_contains "$koshkit_help" \
  "      --color${escape}[2m=<...>${escape}[0m" koshkit-flag
check_excludes "$main_help$builtin_help$koshkit_help" \
  "${escape}[1;37m" bold-white
