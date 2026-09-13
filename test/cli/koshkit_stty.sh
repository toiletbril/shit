unset KOSH_FLAGS
. ./capture-terminal-command.sh

BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d)
d=$(cd "$d" && pwd -P)
trap 'test -n "$d" && /bin/rm -r "$d"' EXIT

command_text="exec \"$BIN\" -c 'before=\$(koshkit stty -g) || exit
echo
koshkit stty KOSH_MISSING_SETTING 2>&1
overflow=10000000000000000:\${before#*:}
if koshkit stty \"\$overflow\" 2>/dev/null; then echo invalid=failed; else echo invalid=passed; fi
koshkit stty -echo igncr -opost tostop erase \"^H\" || exit
settings=\$(koshkit stty -a) || exit
case \"\$settings\" in *-echo*igncr*-opost*tostop*) echo modes=passed;; *) echo modes=failed;; esac
changed=\$(koshkit stty -g) || exit
if [ \"\$before\" != \"\$changed\" ]; then echo changed=passed; else echo changed=failed; fi
koshkit stty \"\$before\" || exit
after=\$(koshkit stty -g) || exit
if [ \"\$before\" = \"\$after\" ]; then echo restore=passed; else echo restore=failed; fi'"

output_pattern='^(3:14: error: stty: invalid terminal setting\.|     3 \|  koshkit stty KOSH_MISSING_SETTING|       \|               \^~~~~~~~~~~~~~~~~~~~|note: read the current terminal settings with `stty -a`\.|(invalid|modes|changed|restore)=)'
NO_COLOR=1 capture_terminal_command "$command_text" | grep -E "$output_pattern"
