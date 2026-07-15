#!/bin/sh

root=$(mktemp -d)
trap '[ -n "$root" ] && /bin/rm -rf "$root"' EXIT
transport=$root/transport
remote=$root/remote
install=$remote/bin
binary=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
/bin/mkdir -p "$transport" "$install"

failure_runner=$root/failure-runner
cat > "$failure_runner" <<'SH'
#!/bin/sh
case $0 in
    *.upload|*.candidate) exec "$ASSIMILATE_RUNNER" "$@" ;;
    */shit)
        install_directory=${0%/*}
        for lock in "$install_directory"/.shit-assimilate-*.lock; do :; done
        /bin/ln -s . "$lock/committed"
        exec "$ASSIMILATE_RUNNER" "$@"
        ;;
    *) exec "$ASSIMILATE_RUNNER" "$@" ;;
esac
SH
chmod +x "$failure_runner"

cat > "$transport/scp" <<'SH'
#!/bin/sh
[ "$1" = -p ] || exit 40
shift
[ "$1" = -- ] || exit 41
shift
[ "$#" -eq 2 ] || exit 42
destination=$2
source=$1
[ "${ASSIMILATE_FAILURE-}" != rollback ] || source=$ASSIMILATE_FAILURE_RUNNER
case $destination in
    *']:'*) remote_name=${destination#*]:} ;;
    *) remote_name=${destination#*:} ;;
esac
/bin/cp -p "$source" "$ASSIMILATE_REMOTE/$remote_name"
[ "${ASSIMILATE_FAILURE-}" != identity ] || printf x >> "$ASSIMILATE_REMOTE/$remote_name"
[ "${ASSIMILATE_FAIL_SCP-0}" -eq 0 ] || exit 23
SH
chmod +x "$transport/scp"

cat > "$transport/ssh" <<'SH'
#!/bin/sh
[ "$1" = -- ] || exit 40
shift
[ "$1" = 'user@[2001:db8::1]' ] || exit 41
shift
[ "$#" -eq 1 ] || exit 42
command=$1
case $command in
    *'/bin/'*) exit 43 ;;
    "exec ./.shit-assimilate-"*.upload*) ;;
    "exec './.shit-assimilate-"*.upload*) ;;
    *) exit 44 ;;
esac
cd "$ASSIMILATE_REMOTE" || exit 1
umask 077
PATH=$ASSIMILATE_REMOTE_PATH /bin/sh -c "$command"
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

run_assimilate()
{
    PATH="$transport:/bin:/usr/bin" "$BIN" -c \
        "assimilate 'user@[2001:db8::1]'" \
        >/dev/null 2>&1
}

export ASSIMILATE_REMOTE=$remote
export ASSIMILATE_REMOTE_PATH="$remote/missing:$install"
export ASSIMILATE_FAILURE_RUNNER=$failure_runner
export ASSIMILATE_RUNNER=$binary
run_assimilate
assimilate_status=$?
if [ "$assimilate_status" -eq 0 ] && cmp "$BIN" "$install/shit" >/dev/null &&
    [ -x "$install/shit" ]; then
    success_status=0
else
    success_status=1
fi
printf 'success=%s leftovers=%s\n' "$success_status" "$(leftovers)"

stale_id=999999-1
stale_lock=$install/.shit-assimilate-$stale_id.lock
physical_install=$(cd "$install" && pwd -P)
/bin/mkdir "$install/.shit-assimilate-888888-1.lock"
/bin/mkdir "$stale_lock"
printf '999999:%s\n' "${stale_lock##*/}" > "$stale_lock/owner"
printf '.shit-assimilate-%s.upload\n' "$stale_id" > "$stale_lock/upload"
printf '%s/.shit-assimilate-%s.candidate\n' "$physical_install" "$stale_id" \
    > "$stale_lock/candidate"
printf 'identity-old\n' > "$stale_lock/backup"
printf 'identity-rejected\n' > "$install/shit"
: > "$stale_lock/had-target"
: > "$stale_lock/rollback"
: > "$remote/.shit-assimilate-$stale_id.upload"
: > "$install/.shit-assimilate-$stale_id.candidate"
export ASSIMILATE_FAILURE=identity
run_assimilate
identity_status=$?
printf 'identity-status=%s old=%s leftovers=%s\n' "$identity_status" \
    "$(cat "$install/shit")" "$(leftovers)"
unset ASSIMILATE_FAILURE

printf 'regular-old\n' > "$install/shit"
chmod 751 "$install/shit"
export ASSIMILATE_FAILURE=rollback
run_assimilate
rollback_status=$?
printf 'rollback-status=%s old=%s mode=%s leftovers=%s\n' "$rollback_status" \
    "$(cat "$install/shit")" "$(file_mode "$install/shit")" "$(leftovers)"
unset ASSIMILATE_FAILURE

printf 'transfer-old\n' > "$install/shit"
export ASSIMILATE_FAIL_SCP=1
run_assimilate
transfer_status=$?
printf 'transfer-status=%s old=%s leftovers=%s\n' "$transfer_status" \
    "$(cat "$install/shit")" "$(leftovers)"
