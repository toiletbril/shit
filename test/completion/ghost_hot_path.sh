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

filesystem_result=$(PATH=/bin "$BIN" --debug-ghost-at "echo $d/probe")
printf '%s\n' "$filesystem_result" | grep -q '^count=2$'
printf '%s\n' "$filesystem_result" | grep -q "^prefix=$d/probe-$"
printf '%s\n' "$filesystem_result" | grep -q '^source-scans=2$'
printf '%s\n' "$filesystem_result" | grep -q '^materialized=0$'
echo 'filesystem ghost streams its prefix'
