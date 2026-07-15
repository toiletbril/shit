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
if [ "${OS-}" = Windows_NT ]; then
    path_separator='\'
    path_delimiter=';'
else
    path_separator='/'
    path_delimiter=':'
fi
echo "== command position offers the executable and the directory, not the data file:"
"$BIN" --debug-complete-at './' </dev/null
echo "== argument position still offers every file:"
"$BIN" --debug-complete-at 'cat ./' </dev/null
echo "== PATH command completion offers only the executable"
"$BIN" --debug-complete-at 'zz_path_' </dev/null
echo "== a blocked first PATH entry does not hide a later executable"
/bin/mkdir "$dir/blocked-first" "$dir/blocked-second"
: > "$dir/blocked-first/zz_path_blocked"
printf '#!/bin/sh\n' > "$dir/blocked-second/zz_path_blocked"
/bin/chmod +x "$dir/blocked-second/zz_path_blocked"
PATH="$dir/blocked-first$path_delimiter$dir/blocked-second" \
    "$BIN" --debug-complete-at 'zz_path_b' </dev/null

/bin/mkdir -p "$dir/native/file-probe"
native_result=$("$BIN" --debug-complete-at \
    "native${path_separator}file" </dev/null)
case "$native_result" in
    *"native${path_separator}file-probe${path_separator}"*) ;;
    *) exit 1 ;;
esac
echo "== native path separators complete directories"

if [ "${OS-}" = Windows_NT ]; then
    : > "$dir/mixedprobe.exe"
    mixed_expected=mixedprobe
else
    printf '#!/bin/sh\n' > "$dir/MIXEDPROBE"
    /bin/chmod +x "$dir/MIXEDPROBE"
    mixed_expected=MIXEDPROBE
fi
mixed_result=$("$BIN" --debug-complete-at 'MIXEDP' </dev/null)
case "$mixed_result" in
    *"$mixed_expected"*) ;;
    *) exit 1 ;;
esac
echo "== native PATH case matching completes commands"

if [ "${OS-}" = Windows_NT ]; then
    : > "$dir/globcase.txt"
    glob_result=$("$BIN" --debug-complete-at 'cat GLOB*.TXT' </dev/null)
    case "$glob_result" in
        *globcase.txt*) ;;
        *) exit 1 ;;
    esac
fi
echo "== native filesystem case matching completes globs"
