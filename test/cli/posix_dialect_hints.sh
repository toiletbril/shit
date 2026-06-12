unset SHIT_FLAGS
# POSIX mode names the owning dialect when a bashism trips its parse.
"$BIN" -P -c 'true |& cat' 2>&1 | head -1
"$BIN" -P -c 'for ((i=0;i<2;i++)); do echo $i; done' 2>&1 | head -1
"$BIN" -P -c 'read -r x <<< hi' 2>&1 | head -1
echo "rc=$?"
