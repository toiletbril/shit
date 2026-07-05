unset SHIT_FLAGS
# set -o posix mirrors set --mood sh, entering the POSIX mood. set +o posix
# steps down to bash only when already in POSIX, and is a no-op otherwise since
# the prior mood is not recoverable.
echo "== set -o posix enters posix mood:"
"$BIN" -c 'set -o posix; set -o | grep posix'
echo "== set +o posix from bash mood is a no-op:"
"$BIN" -M bash -c 'set +o posix; set -o | grep posix'
echo "== set +o posix from posix mood steps to bash:"
"$BIN" -M bash -c 'set -o posix; set +o posix; set -o | grep posix'
echo "== brew's two failing lines now pass:"
"$BIN" -M bash -c 'set +o posix; builtin enable compgen unset; echo ok'
echo "rc-done"
