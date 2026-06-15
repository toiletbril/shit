unset SHIT_FLAGS
# -W surfaces an unset variable in the ${name:+word} alternate form as a warning,
# while a plain run leaves the unset-safe alternate alone and a set name does not
# warn.
echo "== -W warns on an unset :+:"
"$BIN" -W -c 'echo "[${UNSETV:+x}]"' 2>&1 | grep -c "is not set"
echo "== plain run is silent:"
"$BIN" --mood bash -c 'echo "[${UNSETV:+x}]"' 2>&1 | grep -c "is not set"
echo "== a set name does not warn under -W:"
"$BIN" -W -c 'V=val; echo "[${V:+y}]"' 2>&1 | grep -c "is not set"
