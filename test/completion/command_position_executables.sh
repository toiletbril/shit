# Command-position and argument completion retain their matching entry kinds.
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
cd "$dir"
if [ "${OS-}" = Windows_NT ]; then
    path_separator='\'
    path_source_separator='\\'
    path_delimiter=';'
else
    path_separator='/'
    path_source_separator='/'
    path_delimiter=':'
fi
echo "== command position offers a matching executable:"
"$BIN" --debug-complete-at './r' </dev/null
echo "== command position offers a matching directory:"
"$BIN" --debug-complete-at './s' </dev/null
echo "== argument position offers a matching data file:"
"$BIN" --debug-complete-at 'cat ./d' </dev/null
echo "== PATH command completion offers a matching executable:"
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-complete-at 'zz_path_e' </dev/null
echo "== a blocked first PATH entry does not hide a later executable"
mkdir "$dir/blocked-first" "$dir/blocked-second"
: > "$dir/blocked-first/zz_path_blocked"
printf '#!/bin/sh\n' > "$dir/blocked-second/zz_path_blocked"
chmod +x "$dir/blocked-second/zz_path_blocked"
env -u PATH \
    "$TEST_PATH_ENVIRONMENT_NAME=$dir/blocked-first$path_delimiter$dir/blocked-second" \
    "$BIN" --debug-complete-at 'zz_path_b' </dev/null

mkdir -p "$dir/native/file-probe"
native_result=$("$BIN" --debug-complete-at \
    "native${path_source_separator}file" </dev/null)
case "$native_result" in
    *file-probe*"${path_separator}"*) ;;
    *) exit 1 ;;
esac
echo "== native path separators complete directories"

if [ "${OS-}" = Windows_NT ]; then
    : > "$dir/mixedprobe.exe"
    mixed_expected=mixedprobe
else
    printf '#!/bin/sh\n' > "$dir/MIXEDPROBE"
    chmod +x "$dir/MIXEDPROBE"
    mixed_expected=MIXEDPROBE
fi
mixed_result=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-complete-at 'MIXEDP' </dev/null)
case "$mixed_result" in
    *"$mixed_expected"*) ;;
    *) exit 1 ;;
esac
mixed_lower_result=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-complete-at 'mixedp' </dev/null)
case "$mixed_lower_result" in
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
