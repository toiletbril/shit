#!/bin/bash
# exec reads -a, -l, and the -- terminator before the command word, the way bash
# does. -a names the zeroth argument the program reads, -l prefixes that argument
# with a dash so the program reads itself as a login shell, and the two combine
# in a cluster. Each exec runs inside a command substitution, so it replaces the
# contained child rather than the script, and the script survives to print every
# case. The zeroth argument is read back from /proc, and both shells read the
# same place so the comparison holds where /proc exists.
echo "plain=$(exec -a foo cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "login=$(exec -a foo -l cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "cluster=$(exec -la foo cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "attached=$(exec -afoo cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "terminator=$(exec -- cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "alive=yes"


# A file the shell found but cannot execute exits with status 126 rather than
# 127, whether it lacks execute permission or carries binary content in its head.
d=$(mktemp -d)
printf 'echo nope\n' > "$d/noperm"
chmod -x "$d/noperm"
"$d/noperm" 2>/dev/null
echo "noperm=$?"
head -c 4 /dev/zero > "$d/bin"
printf 'binary\n' >> "$d/bin"
chmod +x "$d/bin"
"$d/bin" 2>/dev/null
echo "binary=$?"
rm -rf "$d"

# An executable file with no shebang runs as a shell script rather than failing,
# the way bash falls back when execve reports ENOEXEC.
d=$(mktemp -d)
printf 'echo no-shebang-ran\n' > "$d/s"
chmod +x "$d/s"
"$d/s"
echo "exit=$?"

"$d/s" > "$d/redirected"
cat "$d/redirected"

(exec "$d/s" > "$d/exec-redirected")
echo "exec-exit=$?"
cat "$d/exec-redirected"
rm -rf "$d"

# exec -c runs the command with an empty environment, so env prints nothing. A
# plain exec keeps the environment, so an exported marker still shows. Each exec
# runs inside a command substitution or a pipeline stage, so it replaces the
# contained child and the script survives to print both cases.
export MARKER=present
echo "cleared=[$(exec -c env)]"
echo "kept=$(exec env | grep '^MARKER=')"
echo "alive=yes"
