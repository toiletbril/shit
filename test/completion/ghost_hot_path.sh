set -e

d=$(mktemp -d)
trap 'test -n "$d" && /bin/rm -rf "$d"' EXIT

for name in probe-alpha probe-beta; do
    printf '#!/bin/sh\n' > "$d/$name"
    chmod +x "$d/$name"
done

command_result=$(PATH="$d" "$BIN" --debug-ghost-at 'probe')
printf '%s\n' "$command_result" | grep -q '^count=2$'
printf '%s\n' "$command_result" | grep -q '^prefix=probe-$'
printf '%s\n' "$command_result" | grep -Eq '^source-scans=[1-9][0-9]*$'
printf '%s\n' "$command_result" | grep -q '^materialized=0$'
echo 'command ghost streams its prefix'

mkdir "$d/large"
cp "$d/probe-alpha" "$d/probe-beta" "$d/large"
unrelated_index=0
while [ "$unrelated_index" -lt 128 ]; do
    printf '#!/bin/sh\n' > "$d/large/unrelated-$unrelated_index"
    unrelated_index=$((unrelated_index + 1))
done
chmod +x "$d/large/"*
large_result=$(PATH="$d/large" "$BIN" --debug-ghost-at 'probe')
small_scan_count=
large_scan_count=
while IFS='=' read -r field value; do
    if [ "$field" = source-scans ]; then small_scan_count=$value; fi
done <<EOF
$command_result
EOF
while IFS='=' read -r field value; do
    if [ "$field" = source-scans ]; then large_scan_count=$value; fi
done <<EOF
$large_result
EOF
test "$small_scan_count" = "$large_scan_count"
test "$small_scan_count" -le 128
printf '%s\n' "$large_result" | grep -q '^count=2$'
printf '%s\n' "$large_result" | grep -q '^prefix=probe-$'
printf '%s\n' "$large_result" | grep -q '^materialized=0$'
echo 'command ghost skips unrelated PATH names'

duplicate_result=$(PATH=/bin "$BIN" --debug-ghost-at 'ec')
printf '%s\n' "$duplicate_result" | grep -q '^count=1$'
printf '%s\n' "$duplicate_result" | grep -q '^prefix=echo$'
printf '%s\n' "$duplicate_result" | grep -q '^materialized=0$'
echo 'command ghost deduplicates sources'

filesystem_result=$(PATH=/bin "$BIN" --debug-ghost-at "echo $d/probe")
printf '%s\n' "$filesystem_result" | grep -q '^count=2$'
printf '%s\n' "$filesystem_result" | grep -q "^prefix=$d/probe-$"
printf '%s\n' "$filesystem_result" | grep -q '^source-scans=3$'
printf '%s\n' "$filesystem_result" | grep -q '^materialized=0$'
echo 'filesystem ghost streams its prefix'

if [ "${OS-}" != Windows_NT ]; then
    mkdir "$d/identity"
    printf '#!/bin/sh\n' > "$d/identity/identity-probe"
    chmod +x "$d/identity/identity-probe"
    /bin/ln -s "$d/identity" "$d/identity-alias"
    identity_result=$(PATH="$d/identity-alias" "$BIN" \
        --debug-ghost-at "echo $d/identity/identity")
    printf '%s\n' "$identity_result" | grep -q '^directory-stats=2$'
    printf '%s\n' "$identity_result" | grep -q '^directory-reads=1$'
fi
echo 'directory aliases share one listing'
