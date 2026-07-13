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

PATH="$dir/blocked:$dir/two:/bin" "$BIN" -c \
    'compgen -c >/dev/null 2>&1; blockedprobe >/dev/null 2>&1; printf "%s\n" "$?"'

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

PATH=/bin "$BIN" -c \
    'compfunc() { :; }; eval "alias compalias=:"; compgen -c shopt 2>/dev/null; compgen -c compfunc 2>/dev/null; compgen -c compalias 2>/dev/null'

if [ "${OS-}" != Windows_NT ]; then
    mkdir "$dir/broken-first" "$dir/broken-second"
    /bin/ln -s "$dir/missing" "$dir/broken-first/brokenprobe"
    printf '#!/bin/sh\necho unbroken\n' > "$dir/broken-second/brokenprobe"
    chmod +x "$dir/broken-second/brokenprobe"
    resolved=$(PATH="$dir/broken-first:$dir/broken-second:/bin" "$BIN" -c \
        'brokenprobe; compgen -c >/dev/null 2>&1; brokenprobe')
    if [ "$resolved" != "unbroken
unbroken" ]; then
        exit 1
    fi
fi
printf 'broken-symlink\n'

if [ "${OS-}" = Windows_NT ]; then
    mkdir "$dir/priority"
    : > "$dir/priority/priority.com"
    : > "$dir/priority/priority.exe"
    resolved=$(PATH="$dir/priority" "$BIN" -c \
        'compgen -c >/dev/null; command -v priority')
    case "$resolved" in
        *.exe) ;;
        *) exit 1 ;;
    esac

    resolved=$(PATH="$dir/priority" "$BIN" -c \
        'compgen -c PRIORITY 2>/dev/null')
    case "$resolved" in
        *priority*) ;;
        *) exit 1 ;;
    esac

    resolved=$(PATH="$dir/priority" "$BIN" -c \
        'command -v priority.com >/dev/null; command -v priority')
    case "$resolved" in
        *.exe) ;;
        *) exit 1 ;;
    esac

    mkdir "$dir/priority-first" "$dir/priority-second"
    : > "$dir/priority-first/cross.com"
    : > "$dir/priority-second/cross.exe"
    resolved=$(PATH="$dir/priority-first:$dir/priority-second" "$BIN" -c \
        'command -v cross.exe >/dev/null; command -v cross')
    case "$resolved" in
        *.com) ;;
        *) exit 1 ;;
    esac
else
    mkdir "$dir/suffix-isolation"
    printf '#!/bin/sh\n' > "$dir/suffix-isolation/suffix-only.exe"
    chmod +x "$dir/suffix-isolation/suffix-only.exe"
    if PATH="$dir/suffix-isolation" "$BIN" -c \
        'command -v suffix-only >/dev/null'
    then
        exit 1
    fi
fi
printf 'suffix-priority\n'
