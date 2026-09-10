dir=$(mktemp -d)
trap '[ -n "$dir" ] && /bin/rm -rf "$dir"' EXIT

# A loop body append opens its file once and keeps the descriptor for the whole
# loop. Removing the file mid-loop leaves the later writes on the original
# inode, so the file never comes back. Bash reopens on every iteration and would
# recreate it.
plain_script="$dir/plain.kosh"
{
  printf 'cd "$1" || exit 1\n'
  printf 'for i in 1 2 3; do\n'
  printf '  echo "$i" >> plain.log\n'
  printf '  if [ "$i" = 1 ]; then /bin/rm -f plain.log; fi\n'
  printf 'done\n'
  printf 'if [ -f plain.log ]; then\n'
  printf '  printf "plain=present:"; cat plain.log\n'
  printf 'else\n'
  printf '  echo plain=absent\n'
  printf 'fi\n'
} > "$plain_script"
"$BIN" --mood bash "$plain_script" "$dir"

# A pipeline stage holds the descriptor the same way.
stage_script="$dir/stage.kosh"
{
  printf 'cd "$1" || exit 1\n'
  printf 'for i in 1 2 3; do\n'
  printf '  echo "$i" >> stage.log | cat\n'
  printf '  if [ "$i" = 1 ]; then /bin/rm -f stage.log; fi\n'
  printf 'done\n'
  printf 'if [ -f stage.log ]; then\n'
  printf '  printf "stage=present:"; cat stage.log\n'
  printf 'else\n'
  printf '  echo stage=absent\n'
  printf 'fi\n'
} > "$stage_script"
"$BIN" --mood bash "$stage_script" "$dir"

# Every iteration still reaches the file, and a stage that also redirects its
# error stream keeps a second descriptor of its own.
content_script="$dir/content.kosh"
{
  printf 'cd "$1" || exit 1\n'
  printf 'for i in 1 2 3; do\n'
  printf '  { echo "out-$i" >> out.log; echo "err-$i" 2>> err.log 1>&2; } | cat\n'
  printf 'done\n'
  printf 'printf "out=%%s\\n" "$(cat out.log | tr "\\n" ",")"\n'
  printf 'printf "err=%%s\\n" "$(cat err.log | tr "\\n" ",")"\n'
} > "$content_script"
"$BIN" --mood bash "$content_script" "$dir"

# The cache holds sixteen descriptors. A loop that appends to more files than
# that keeps working, because a target past the limit is reopened every round.
bound_script="$dir/bound.kosh"
{
  printf 'cd "$1" || exit 1\n'
  printf 'n=0\n'
  printf 'while [ "$n" -lt 40 ]; do\n'
  printf '  n=$((n+1))\n'
  printf '  echo body >> "bound-$n.log" | cat\n'
  printf 'done\n'
  printf 'count=0\n'
  printf 'for f in bound-*.log; do count=$((count+1)); done\n'
  printf 'printf "bound=%%s\\n" "$count"\n'
} > "$bound_script"
"$BIN" --mood bash "$bound_script" "$dir"

# The descriptor belongs to the loop, not to the shell. A second loop over the
# same file opens it again and appends after what the first loop wrote.
reopen_script="$dir/reopen.kosh"
{
  printf 'cd "$1" || exit 1\n'
  printf 'for i in a b; do echo "$i" >> reopen.log | cat; done\n'
  printf 'for i in c d; do echo "$i" >> reopen.log | cat; done\n'
  printf 'printf "reopen=%%s\\n" "$(cat reopen.log | tr "\\n" ",")"\n'
} > "$reopen_script"
"$BIN" --mood bash "$reopen_script" "$dir"

# A truncating redirection is never memoized. Each iteration reopens the file
# and the last write is all that is left.
truncate_script="$dir/truncate.kosh"
{
  printf 'cd "$1" || exit 1\n'
  printf 'for i in 1 2 3; do echo "$i" > truncate.log | cat; done\n'
  printf 'printf "truncate=%%s\\n" "$(cat truncate.log | tr "\\n" ",")"\n'
} > "$truncate_script"
"$BIN" --mood bash "$truncate_script" "$dir"

echo done
