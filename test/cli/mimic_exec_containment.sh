unset KOSH_FLAGS
# A mimicked script that points stdin away with an exec redirection must not
# leave the parent shell's descriptors moved, the way a fork would have
# contained it. configure does exactly this and the interactive prompt used to
# die on raw mode afterwards.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf '#!/bin/bash\nexec </dev/null\necho script-ran\n' > "$dir/probe.sh"
chmod +x "$dir/probe.sh"
out=$(echo data | "$BIN" --mood bash -I -c "$dir/probe.sh; read -r line && echo got=\$line || echo stdin-lost")
rc=$?
printf '%s\n' "$out"
echo "rc=$rc"

# A mimicked script runs inside this process, and the backups taken for its
# standard descriptors stay open underneath it. Bash replaces the process and
# leaves none behind. The low numbers have to read as closed here too. The
# script opens one of them itself to prove the scan can see an open descriptor.
printf '%s\n' \
  '#!/bin/bash' \
  'visible=""' \
  'for n in 11 12 13; do' \
  '  if : >&"$n"; then visible="$visible $n"; fi' \
  'done 2>/dev/null' \
  'printf "reachable:%s\n" "$visible"' \
  'exec 11>"$1"' \
  'if : >&11; then echo "own=usable"; fi' \
  'exec 11>&-' > "$dir/fds.sh"
chmod +x "$dir/fds.sh"
"$BIN" --mood bash -I -c "$dir/fds.sh $dir/eleven"
echo "fds-rc=$?"
