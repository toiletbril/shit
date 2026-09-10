unset KOSH_FLAGS
# A signal the shell inherits as ignored is listed with the empty action, and an
# action installed for it afterwards does not replace that listing. The
# reference shell behaves the same way.
echo "== a signal ignored at entry lists with an empty action:"
( trap "" USR1; "$BIN" --mood bash -c 'trap -p USR1; echo sep; trap -p' )
echo "rc=$?"
echo "== an action installed for that signal does not replace the listing:"
( trap "" USR1
  "$BIN" --mood bash -c \
    'trap "echo caught" USR1; trap -p USR1; echo sep; trap -p' )
echo "rc=$?"
echo "== a signal that is not ignored at entry lists its action:"
"$BIN" --mood bash -c 'trap "echo caught" USR1; trap -p USR1'
echo "rc=$?"
