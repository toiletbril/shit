#!/bin/sh

root=$(mktemp -d)
trap '[ -n "$root" ] && /bin/rm -rf "$root"' EXIT
transport=$root/transport
remote=$root/remote
first=$remote/first
second=$remote/second
log=$root/transport.log
binary=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
/bin/mkdir -p "$transport" "$remote" "$first" "$second"

cat > "$transport/scp" <<'SH'
#!/bin/sh
[ "$1" = -- ] && shift
source=$1
destination=$2
target=${destination%%:*}
remote_name=${destination#*:}
printf 'scp-target=%s\n' "$target" >> "$ASSIMILATE_LOG"
/bin/cp "${ASSIMILATE_PAYLOAD-$source}" "$ASSIMILATE_REMOTE/$remote_name"
[ "${ASSIMILATE_FAIL_SCP-0}" -eq 0 ] || exit 23
SH
chmod +x "$transport/scp"

cat > "$transport/ssh" <<'SH'
#!/bin/sh
[ "$1" = -- ] && shift
target=$1
shift
printf 'ssh-target=%s args=%s\n' "$target" "$#" >> "$ASSIMILATE_LOG"
if [ "${ASSIMILATE_FAIL_SSH_START-0}" -eq 1 ]; then
    case $1 in
        '/bin/rm -f '*) ;;
        *) exit 31 ;;
    esac
fi
cd "$ASSIMILATE_REMOTE" || exit 1
PATH=$ASSIMILATE_REMOTE_PATH /bin/sh -c "$1"
SH
chmod +x "$transport/ssh"

export ASSIMILATE_LOG=$log
export ASSIMILATE_REMOTE=$remote
export ASSIMILATE_REMOTE_PATH="$remote/missing:$first:$second:/bin:/usr/bin"
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate user@host' >/dev/null
cmp "$BIN" "$first/shit" >/dev/null && [ -x "$first/shit" ]
printf 'success=%s\n' "$?"
cat "$log"

printf 'old-install\n' > "$first/shit"
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null
cmp "$BIN" "$first/shit" >/dev/null && [ -x "$first/shit" ]
printf 'replace=%s leftovers=%s\n' "$?" \
    "$(find "$remote" -name '.shit-assimilate-*' | wc -l | tr -d ' ')"

printf 'old-install\n' > "$first/shit"
export ASSIMILATE_FAIL_SCP=1
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
printf 'upload-status=%s old=%s leftovers=%s\n' "$?" \
    "$(cat "$first/shit")" \
    "$(find "$remote" -name '.shit-assimilate-*' | wc -l | tr -d ' ')"
unset ASSIMILATE_FAIL_SCP

export ASSIMILATE_FAIL_SSH_START=1
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
printf 'ssh-start-status=%s old=%s leftovers=%s\n' "$?" \
    "$(cat "$first/shit")" \
    "$(find "$remote" -name '.shit-assimilate-*' | wc -l | tr -d ' ')"
unset ASSIMILATE_FAIL_SSH_START

export ASSIMILATE_REMOTE_PATH=$remote/missing
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
printf 'no-path-status=%s old=%s leftovers=%s\n' "$?" \
    "$(cat "$first/shit")" \
    "$(find "$remote" -name '.shit-assimilate-*' | wc -l | tr -d ' ')"

payload=$root/failing-payload
cat > "$payload" <<'SH'
#!/bin/sh
case $0 in
    */shit) exit 1 ;;
    *) exit 0 ;;
esac
SH
chmod +x "$payload"
old_target=$remote/old-target
printf 'old-target\n' > "$old_target"
/bin/rm -f "$first/shit"
ln -s "$old_target" "$first/shit"
export ASSIMILATE_REMOTE_PATH="$first:/bin:/usr/bin"
export ASSIMILATE_PAYLOAD=$payload
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
rollback_status=$?
if [ -L "$first/shit" ] && [ "$(readlink "$first/shit")" = "$old_target" ]; then
    rollback=restored
else
    rollback=damaged
fi
printf 'rollback-status=%s symlink=%s leftovers=%s\n' "$rollback_status" \
    "$rollback" \
    "$(find "$remote" -name '.shit-assimilate-*' | wc -l | tr -d ' ')"
unset ASSIMILATE_PAYLOAD

precommit_payload=$root/precommit-payload
printf '#!/bin/sh\nexit 1\n' > "$precommit_payload"
chmod +x "$precommit_payload"
printf 'old-install\n' > "$first/shit"
export ASSIMILATE_PAYLOAD=$precommit_payload
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
printf 'precommit-status=%s old=%s leftovers=%s\n' "$?" \
    "$(cat "$first/shit")" \
    "$(find "$remote" -name '.shit-assimilate-*' | wc -l | tr -d ' ')"
unset ASSIMILATE_PAYLOAD

/bin/rm -f "$first/shit"
/bin/mkdir "$first/shit"
printf 'directory-marker\n' > "$first/shit/marker"
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate host' >/dev/null 2>&1
printf 'directory-status=%s marker=%s leftovers=%s\n' "$?" \
    "$(cat "$first/shit/marker")" \
    "$(find "$remote" -name '.shit-assimilate-*' | wc -l | tr -d ' ')"

: > "$log"
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate' >/dev/null 2>&1
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate --bad' >/dev/null 2>&1
PATH="$transport:/bin:/usr/bin" "$BIN" -c 'assimilate one two' >/dev/null 2>&1
printf 'invalid-transport-lines=%s\n' "$(wc -l < "$log" | tr -d ' ')"

/bin/rm -rf "$first/shit"
/bin/mkdir -p "$root/shellbin"
ln -s "$binary" "$root/shellbin/shit"
PATH="$root/shellbin:$transport:/bin:/usr/bin" ../scripts/shit-scp host >/dev/null
cmp "$BIN" "$first/shit" >/dev/null && [ -x "$first/shit" ]
printf 'wrapper=%s\n' "$?"
