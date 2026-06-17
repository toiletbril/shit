unset SHIT_FLAGS
# The [[ conditional is a bash addition, so the sh mood rejects it while the
# default and bash moods run it.
"$BIN" -c '[[ -n x ]] && echo ran'
"$BIN" --mood bash -c '[[ -n x ]] && echo ran'
"$BIN" --mood sh -c '[[ -n x ]] && echo ran' 2>&1 | head -1
echo "rc=$?"
