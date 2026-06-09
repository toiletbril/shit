#!/bin/sh
# -L and -h test the symlink-ness of a path without following it.
target=/tmp/shit_symlink_target_fixed
link=/tmp/shit_symlink_link_fixed
rm -f "$link"
: > "$target"
ln -s "$target" "$link"
[ -L "$link" ] && echo link_is_symlink
[ -h "$link" ] && echo link_h_symlink
[ ! -L "$target" ] && echo target_not_symlink
[ -L "$target" ] || echo target_confirmed_plain
[ ! -L "$link" ] || echo negation_of_symlink_false
rm -f "$link" "$target"
# Other POSIX file-type primaries, deterministic against a redirected stdout.
[ -c /dev/null ] && echo devnull_is_char
[ -b /dev/null ] || echo devnull_not_block
[ -t 1 ] && echo stdout_is_tty || echo stdout_not_tty
[ -p /etc ] || echo etc_not_fifo
[ -S /etc ] || echo etc_not_socket
