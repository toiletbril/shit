#!/bin/sh

root=$TEST_TEMP_DIRECTORY/goodfsw-trx
event_output=$TEST_TEMP_DIRECTORY/goodfsw-trx.out
mkdir -p "$root/sub"
: > "$root/sub/file"
: > "$event_output"

(
  while [ ! -s "$event_output" ]; do
    printf x >> "$root/sub/file"
    sleep 0.02
  done
) &
writer_pid=$!

"$BIN" -c 'koshkit goodfsw -trx1 -l 0.02 "$1"' goodfsw "$root" \
  > "$event_output" 2>&1
watcher_status=$?
wait "$writer_pid"
IFS=' ' read -r event_time event_path event_name event_kind < "$event_output"
case $event_time in
  ''|*[!0-9]*) event_time=nonnumeric ;;
  *) event_time=numeric ;;
esac

printf 'status=%s\n' "$watcher_status"
printf 'timestamp=%s\n' "$event_time"
printf 'path=%s\n' "$event_path"
printf 'event=%s\n' "$event_name"
printf 'kind=%s\n' "$event_kind"

node_root=$TEST_TEMP_DIRECTORY/goodnode-batch
node_path=$node_root/a/b/needle
mkdir -p "$node_root/a/b"
: > "$node_path"
inode=$("$BIN" -c 'koshkit stat -c %i "$1"' stat "$node_path")
node_report=$("$BIN" -c 'koshkit goodnode --color never -r "$1" -i "$2"' \
  goodnode "$node_root" "$inode")
node_status=$?
case $node_report in
  "$node_path"*) node_path_status=matched ;;
  *) node_path_status=wrong ;;
esac
printf 'goodnode-status=%s\n' "$node_status"
printf 'goodnode-path=%s\n' "$node_path_status"

default_report=$("$BIN" -c 'koshkit evilio --color never')
case $default_report in
  MEMORY*DISKS*SWAP*) default_shape=matched ;;
  *) default_shape=wrong ;;
esac
case $default_report in
  *CPU*|*PAGING*|*SCHEDULER*|*STALLS*|*Activity*) default_scope=extra ;;
  *) default_scope=instant ;;
esac
printf 'evilio-default-shape=%s\n' "$default_shape"
printf 'evilio-default-scope=%s\n' "$default_scope"

disk_report=$("$BIN" -c 'koshkit evilio --cumulative 0.05 --color never')
case $disk_report in
  DEVICE*READ/S*WRITE/S*"READ IOPS"*"WRITE IOPS"*) disk_shape=matched ;;
  *) disk_shape=wrong ;;
esac
case $disk_report in
  *CPU*|*MEMORY*|*PAGING*|*SCHEDULER*|*STALLS*|*SWAP*) disk_scope=extra ;;
  *) disk_scope=only-io ;;
esac
printf 'evilio-disk-shape=%s\n' "$disk_shape"
printf 'evilio-disk-scope=%s\n' "$disk_scope"

process_report=$("$BIN" -c \
  'koshkit evilio --cumulative=0.05 --ps --count 1 --color never')
case $process_report in
  *PID*READ/S*WRITE/S*"READ IOPS"*"WRITE IOPS"*COMMAND*)
    process_shape=matched
    ;;
  *) process_shape=wrong ;;
esac
case $process_report in
  *DISKS*|*MEMORY*|*PAGING*|*SCHEDULER*|*STALLS*|*SWAP*)
    process_scope=extra
    ;;
  *) process_scope=only-io ;;
esac
printf 'evilio-process-shape=%s\n' "$process_shape"
printf 'evilio-process-scope=%s\n' "$process_scope"

process_report_path=$TEST_TEMP_DIRECTORY/evilio-process-report
printf '%s\n' "$process_report" > "$process_report_path"
process_idle=excluded
while read -r process_pid process_read process_write process_read_iops \
  process_write_iops process_command; do
  case "$process_pid:$process_read:$process_write:$process_read_iops:$process_write_iops" in
    [0-9]*:0:0:0:0) process_idle=present ;;
    [0-9]*:0:0:-:-) process_idle=present ;;
  esac
done < "$process_report_path"
printf 'evilio-process-idle=%s\n' "$process_idle"

"$BIN" -c \
  'koshkit evilio --cumulative --ps -1 0.05 --color never' \
  > "$TEST_NULL_DEVICE"
printf 'evilio-process-duration-limit=%s\n' "$?"

all_process_report=$(
  "$BIN" -c 'koshkit evilio --ps --color never'
)
limited_process_report=$(
  "$BIN" -c 'koshkit evilio --ps -1 --color never'
)
all_process_line_count=$(printf '%s\n' "$all_process_report" | wc -l)
limited_process_line_count=$(printf '%s\n' "$limited_process_report" | wc -l)
if [ "$all_process_line_count" -gt "$limited_process_line_count" ]; then
  process_limit=unlimited
else
  process_limit=wrong
fi
printf 'evilio-process-limit=%s\n' "$process_limit"

network_report=$("$BIN" -c 'koshkit evilnet')
case $network_report in
  NAME*FAMILY*ADDRESS*) network_title=omitted ;;
  *) network_title=present ;;
esac
printf 'evilnet-single-title=%s\n' "$network_title"

network_all_report=$("$BIN" -c 'koshkit evilnet --all' \
  2> "$TEST_NULL_DEVICE")
case $network_all_report in
  INTERFACES*) network_all_title=present ;;
  *) network_all_title=missing ;;
esac
printf 'evilnet-multiple-titles=%s\n' "$network_all_title"

process_tree=$("$BIN" -c 'koshkit evilps -1')
case $process_tree in
  PROCESSES*) process_title=present ;;
  *) process_title=omitted ;;
esac
printf 'evilps-single-title=%s\n' "$process_title"

filesystem_report=$("$BIN" -c 'koshkit evilfs')
case $filesystem_report in
  SOURCE*TARGET*TYPE*OPTIONS*) filesystem_title=omitted ;;
  *) filesystem_title=present ;;
esac
printf 'evilfs-single-title=%s\n' "$filesystem_title"

filesystem_all_report=$("$BIN" -c 'koshkit evilfs --all')
case $filesystem_all_report in
  FILESYSTEMS*) filesystem_all_title=present ;;
  *) filesystem_all_title=missing ;;
esac
printf 'evilfs-multiple-titles=%s\n' "$filesystem_all_title"

cores_report=$("$BIN" -c 'koshkit evillogs --cores')
case $cores_report in
  CORES*|*LOGS*) cores_title=present ;;
  *) cores_title=omitted ;;
esac
printf 'evillogs-single-title=%s\n' "$cores_title"

logs_report=$("$BIN" -c 'koshkit evillogs --cores --logs')
case $logs_report in
  CORES*LOGS*) logs_titles=present ;;
  *) logs_titles=missing ;;
esac
printf 'evillogs-multiple-titles=%s\n' "$logs_titles"

"$BIN" -c 'koshkit evilio --all --cumulative --color never' \
  > "$TEST_NULL_DEVICE" 2>&1
printf 'evilio-conflict=%s\n' "$?"
