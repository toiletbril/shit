unset KOSH_FLAGS
. ./capture-terminal-command.sh

BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

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

escape=$(printf '\033')
diagnostic=$(NO_COLOR= capture_terminal_command \
  "exec \"$BIN\" -c 'koshkit ls --dasdas'") || exit 1
warning=$(NO_COLOR= capture_terminal_command \
  "exec \"$BIN\" -c 'koshkit retry -n 2 -d 0 false || :'") || exit 1

check_contains "$diagnostic" \
  "${escape}[1;91merror${escape}[0m" error-color
check_contains "$diagnostic" \
  "${escape}[34mkoshkit${escape}[0m ${escape}[34mls${escape}[0m ${escape}[3m--dasdas${escape}[0m" source-colors
check_contains "$diagnostic" \
  "${escape}[1;91m^~~~~~~~${escape}[0m" caret-color
check_contains "$diagnostic" \
  "${escape}[36mnote${escape}[0m: ${escape}[36mUse" note-color
check_contains "$warning" \
  "${escape}[33mwarning${escape}[0m: retry attempt 1 of 2 failed with status 1." warning-color
