#!/bin/sh

root=$(mktemp -d)
trap '[ -n "$root" ] && /bin/rm -rf "$root"' EXIT
transport=$root/transport
remote=$root/remote
install=$remote/bin
log=$root/transport.log
binary=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
/bin/mkdir -p "$transport" "$install"

payload_runner=$root/payload-runner
cat > "$payload_runner" <<'SH'
#!/bin/sh
case $0 in
    *.upload) exec "$ASSIMILATE_RUNNER" "$@" ;;
    *) exec "$ASSIMILATE_PAYLOAD" "$@" ;;
esac
SH
chmod +x "$payload_runner"

cat > "$transport/scp" <<'SH'
#!/bin/sh
[ "$1" = -p ] && shift
[ "$1" = -- ] && shift
destination=$2
printf 'scp=%s\n' "${destination%%:*}" >> "$ASSIMILATE_LOG"
source=$1
[ -z "${ASSIMILATE_PAYLOAD-}" ] || source=$ASSIMILATE_PAYLOAD_RUNNER
/bin/cp -p "$source" "$ASSIMILATE_REMOTE/${destination#*:}"
[ "${ASSIMILATE_FAIL_SCP-0}" -eq 0 ] || exit 23
SH
chmod +x "$transport/scp"

cat > "$transport/ssh" <<'SH'
#!/bin/sh
[ "$1" = -- ] && shift
target=$1
shift
case $1 in
    *'/bin/'*) exit 43 ;;
    "exec ./.shit-assimilate-"*.upload*) ;;
    "exec './.shit-assimilate-"*.upload*) ;;
    *) exit 44 ;;
esac
printf 'ssh=%s\n' "$target" >> "$ASSIMILATE_LOG"
cd "$ASSIMILATE_REMOTE" || exit 1
umask 077
PATH=$ASSIMILATE_REMOTE_PATH /bin/sh -c "$1"
SH
chmod +x "$transport/ssh"

leftovers()
{
    find "$remote" -name '.shit-assimilate-*' | wc -l | tr -d ' '
}

file_mode()
{
    stat -f %Lp "$1" 2>/dev/null || stat -c %a "$1"
}

export ASSIMILATE_LOG=$log
export ASSIMILATE_REMOTE=$remote
export ASSIMILATE_REMOTE_PATH="$remote/missing:$install"
export ASSIMILATE_PAYLOAD_RUNNER=$payload_runner
export ASSIMILATE_RUNNER=$binary
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate user@host' >/dev/null
cmp "$BIN" "$install/shit" >/dev/null && [ -x "$install/shit" ]
printf 'success=%s leftovers=%s\n' "$?" "$(leftovers)"
cat "$log"

printf 'old\n' > "$install/shit"
: > "$install/.shit-assimilate.backup"
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null
cmp "$BIN" "$install/shit" >/dev/null
replace_status=$?
fixed_backup=$([ -e "$install/.shit-assimilate.backup" ] && \
    echo preserved || echo removed)
/bin/rm -f "$install/.shit-assimilate.backup"
printf 'replace=%s fixed-backup=%s leftovers=%s\n' "$replace_status" \
    "$fixed_backup" "$(leftovers)"

failing_payload=$root/failing-payload
printf '#!/bin/sh\nexit 1\n' > "$failing_payload"
chmod +x "$failing_payload"
export ASSIMILATE_PAYLOAD=$failing_payload
/bin/rm -f "$install/shit"
printf 'regular-old\n' > "$install/shit"
chmod 751 "$install/shit"
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
printf 'regular-status=%s old=%s mode=%s leftovers=%s\n' "$?" \
    "$(cat "$install/shit")" "$(file_mode "$install/shit")" "$(leftovers)"

old_target=$remote/old-target
printf 'symlink-old\n' > "$old_target"
/bin/rm -f "$install/shit"
ln -s "$old_target" "$install/shit"
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
if [ -L "$install/shit" ] && [ "$(readlink "$install/shit")" = "$old_target" ]; then
    symlink_status=preserved
else
    symlink_status=damaged
fi
printf 'symlink=%s leftovers=%s\n' "$symlink_status" "$(leftovers)"
unset ASSIMILATE_PAYLOAD

/bin/rm -f "$install/shit"
printf 'transfer-old\n' > "$install/shit"
export ASSIMILATE_FAIL_SCP=1
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
printf 'transfer-status=%s old=%s leftovers=%s\n' "$?" \
    "$(cat "$install/shit")" "$(leftovers)"
unset ASSIMILATE_FAIL_SCP

/bin/rm -f "$install/shit"
/bin/mkdir -p "$root/shellbin"
ln -s "$binary" "$root/shellbin/shit"
PATH="$root/shellbin:$transport:/bin:/usr/bin" ../scripts/shit-scp host >/dev/null
cmp "$BIN" "$install/shit" >/dev/null && [ -x "$install/shit" ]
printf 'wrapper=%s\n' "$?"
