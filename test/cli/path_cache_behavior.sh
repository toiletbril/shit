#!/bin/sh

dir=$(mktemp -d)
trap '[ -n "$dir" ] && /bin/rm -rf "$dir"' EXIT

mkdir "$dir/one" "$dir/two" "$dir/blocked" "$dir/refresh" "$dir/appeared"
printf '#!/bin/sh\necho one\n' > "$dir/one/cacheprobe"
printf '#!/bin/sh\necho two\n' > "$dir/two/cacheprobe"
printf '#!/bin/sh\nexit 0\n' > "$dir/two/blockedprobe"
printf '#!/bin/sh\necho refreshed\n' > "$dir/staged"
printf '#!/bin/sh\necho appeared\n' > "$dir/staged-appeared"
chmod +x "$dir/one/cacheprobe" "$dir/two/cacheprobe"
chmod +x "$dir/two/blockedprobe" "$dir/staged" "$dir/staged-appeared"
: > "$dir/blocked/blockedprobe"

CACHE_SECOND="$dir/two" PATH="$dir/one:/bin" "$BIN" -c \
    'for iteration in 1 2; do cacheprobe; PATH="$CACHE_SECOND:/bin"; done'

PATH="$dir/blocked:$dir/two:/bin" "$BIN" -c \
    'blockedprobe >/dev/null 2>&1; printf "%s\n" "$?"'

CACHE_COMMAND=refreshed CACHE_DIRECTORY="$dir/refresh" \
    CACHE_STAGED="$dir/staged" \
    PATH="$dir/refresh:/bin" "$BIN" -c \
    'command -v "$CACHE_COMMAND" >/dev/null 2>&1; /bin/mv "$CACHE_STAGED" "$CACHE_DIRECTORY/refreshed"; hash -r; "$CACHE_COMMAND"'

CACHE_COMMAND=appeared CACHE_DIRECTORY="$dir/appeared" \
    CACHE_STAGED="$dir/staged-appeared" \
    PATH="$dir/appeared:/bin" "$BIN" -c \
    'command -v "$CACHE_COMMAND" >/dev/null 2>&1; /bin/mv "$CACHE_STAGED" "$CACHE_DIRECTORY/appeared"; "$CACHE_COMMAND"'

PATH="$dir/blocked:$dir/two:/bin" "$BIN" \
    --debug-complete-at 'blockedpr' 2>/dev/null
