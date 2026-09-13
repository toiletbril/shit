if script -q -c true /dev/null >/dev/null 2>&1; then
  TEST_TERMINAL_SCRIPT_STYLE=util-linux
elif script -q /dev/null /usr/bin/true >/dev/null 2>&1; then
  TEST_TERMINAL_SCRIPT_STYLE=bsd
else
  TEST_TERMINAL_SCRIPT_STYLE=unavailable
fi

capture_terminal_command()
{
  command_text=$1
  case "$TEST_TERMINAL_SCRIPT_STYLE" in
    util-linux)
      TERM=xterm script -q -c "$command_text" /dev/null 2>/dev/null |
        tr -d '\r'
      ;;
    bsd)
      TERM=xterm script -q /dev/null /bin/sh -c "$command_text" \
        2>/dev/null | tr -d '\r'
      ;;
    *) return 1 ;;
  esac
}
