unset SHIT_FLAGS
# printf \e and \E emit the escape character in bash moods but pass through
# literally in the sh mood. The %b conversion follows the same gate.
echo "== bash mood printf \\e:"
"$BIN" --mood bash -c 'printf "\e"' | xxd | head -1
echo "== bash mood printf \\E:"
"$BIN" --mood bash -c 'printf "\E"' | xxd | head -1
echo "== sh mood printf \\e stays literal:"
"$BIN" --mood sh -c 'printf "\e"' | xxd | head -1
echo "== bash-posix mood printf \\e:"
"$BIN" --mood bash-posix -c 'printf "\e"' | xxd | head -1
echo "== bash mood %b with \\e:"
"$BIN" --mood bash -c 'printf "%b\n" "\e"' | xxd | head -1
echo "== sh mood %b with \\e stays literal:"
"$BIN" --mood sh -c 'printf "%b\n" "\e"' | xxd | head -1
