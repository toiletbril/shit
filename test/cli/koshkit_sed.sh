unset KOSH_FLAGS

echo "--- anchored global substitution ---"
"$BIN" -c "printf 'ab\\n' | koshkit sed 's/^/x/g'"

echo "--- adjacent empty global match ---"
"$BIN" -c "printf 'ab\\n' | koshkit sed 's/a*/x/g'"

echo "--- unterminated input ---"
"$BIN" -c "printf a | koshkit sed 's/a/b/'; printf '<end>\\n'"

echo "--- script from standard input ---"
sed_data=$TEST_TEMP_DIRECTORY/koshkit-sed-data
printf 'alpha\n' > "$sed_data"
printf 's/alpha/ALPHA/\n' | \
  "$BIN" -c 'koshkit sed -f - "$1"' sed-test "$sed_data"

sed_first=$TEST_TEMP_DIRECTORY/koshkit-sed-first
sed_last=$TEST_TEMP_DIRECTORY/koshkit-sed-last
printf 'first\nsecond\n' > "$sed_first"
printf 'third\n' > "$sed_last"

echo "--- multiple data files with a missing operand ---"
"$BIN" -c \
  'koshkit sed -n "2,3p" "$1" missing.txt "$2"; printf "status=%s\n" "$?"' \
  sed-test "$sed_first" "$sed_last" 2>&1

echo "--- repeated standard input ---"
printf 'left\nright\n' | "$BIN" -c 'koshkit sed -n "1,3p" - -'
