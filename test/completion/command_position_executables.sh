# A command-position path (a token holding a slash in the first word) completes
# only runnable files and directories, the way fish limits the command word.
# Plain data files are dropped there but stay in argument position. PATH is
# pinned to an empty directory so nothing else joins the candidates.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf '#!/bin/sh\n' > "$dir/run"
chmod +x "$dir/run"
: > "$dir/data.txt"
mkdir "$dir/sub"
printf '#!/bin/sh\n' > "$dir/zz_path_exec"
chmod +x "$dir/zz_path_exec"
: > "$dir/zz_path_data"
mkdir "$dir/zz_path_dir"
export PATH="$dir"
cd "$dir"
echo "== command position offers the executable and the directory, not the data file:"
"$BIN" --debug-complete-at './' </dev/null
echo "== argument position still offers every file:"
"$BIN" --debug-complete-at 'cat ./' </dev/null
echo "== PATH command completion offers only the executable"
"$BIN" --debug-complete-at 'zz_path_' </dev/null
