#!/bin/bash
printf 'dbracket=%s\n' "$(type -t '[[')"
printf 'dbracketclose=%s\n' "$(type -t ']]')"
printf 'bang=%s\n' "$(type -t '!')"
printf 'brace=%s\n' "$(type -t '{')"
printf 'function=%s\n' "$(type -t 'function')"
printf 'time=%s\n' "$(type -t 'time')"
printf 'if=%s\n' "$(type -t 'if')"
printf 'echo=%s\n' "$(type -t 'echo')"
