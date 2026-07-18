#!/bin/sh

dir=$(mktemp -d)
trap '[ -n "$dir" ] && /bin/rm -rf "$dir"' EXIT

mkdir "$dir/one" "$dir/two" "$dir/blocked" "$dir/refresh" "$dir/appeared" \
    "$dir/completion-hot" "$dir/completion-late" "$dir/vanished" \
    "$dir/atomic" "$dir/atomic-replacement"
printf '#!/bin/sh\necho one\n' > "$dir/one/cacheprobe"
printf '#!/bin/sh\necho two\n' > "$dir/two/cacheprobe"
printf '#!/bin/sh\nexit 0\n' > "$dir/two/blockedprobe"
printf '#!/bin/sh\necho refreshed\n' > "$dir/staged"
printf '#!/bin/sh\necho appeared\n' > "$dir/staged-appeared"
printf '#!/bin/sh\necho late\n' > "$dir/staged-late"
chmod +x "$dir/one/cacheprobe" "$dir/two/cacheprobe"
chmod +x "$dir/two/blockedprobe" "$dir/staged" "$dir/staged-appeared" \
    "$dir/staged-late"
: > "$dir/blocked/blockedprobe"
printf '#!/bin/sh\n' > "$dir/vanished/vanishprobe"
chmod +x "$dir/vanished/vanishprobe"
printf '#!/bin/sh\n' > "$dir/atomic/oldatomicprobe"
printf '#!/bin/sh\n' > "$dir/atomic-replacement/newatomicprobe"
chmod +x "$dir/atomic/oldatomicprobe" \
    "$dir/atomic-replacement/newatomicprobe"

CACHE_SECOND="$dir/two" PATH="$dir/one:/bin" "$BIN" -c \
    'for iteration in 1 2; do cacheprobe; PATH="$CACHE_SECOND:/bin"; done'

PATH="$dir/blocked:$dir/two:/bin" "$BIN" -c \
    'blockedprobe >/dev/null 2>&1; printf "%s\n" "$?"'

PATH="$dir/blocked:$dir/two:/bin" "$BIN" -c \
    'compgen -c >/dev/null 2>&1; blockedprobe >/dev/null 2>&1; printf "%s\n" "$?"'

mkdir "$dir/warm-first" "$dir/warm-second"
printf '#!/bin/sh\necho stale-first\n' > "$dir/warm-first/warmprobe"
printf '#!/bin/sh\necho warm-second\n' > "$dir/warm-second/warmprobe"
chmod +x "$dir/warm-first/warmprobe" "$dir/warm-second/warmprobe"
WARM_FIRST="$dir/warm-first/warmprobe" \
    PATH="$dir/warm-first:$dir/warm-second:/bin" "$BIN" -c \
    'compgen -c warmprobe >/dev/null 2>&1; /bin/rm -f "$WARM_FIRST"; warmprobe'

mkdir "$dir/blocked-later"
: > "$dir/blocked-later/recoverprobe"
printf '#!/bin/sh\necho recovered\n' > "$dir/staged-recover"
chmod +x "$dir/staged-recover"
RECOVER_DIRECTORY="$dir/blocked-later" RECOVER_STAGED="$dir/staged-recover" \
    PATH="$dir/blocked-later:/bin" "$BIN" -c \
    'recoverprobe >/dev/null 2>&1; /bin/mv "$RECOVER_STAGED" "$RECOVER_DIRECTORY/recoverprobe"; recoverprobe'

printf '#!/bin/sh\n' > "$dir/completion-hot/hotprobe"
chmod +x "$dir/completion-hot/hotprobe"
HOT_PROBE="$dir/completion-hot/hotprobe" \
    LATE_DIRECTORY="$dir/completion-late" LATE_STAGED="$dir/staged-late" \
    PATH="$dir/completion-hot:$dir/completion-late:/bin" "$BIN" -c \
    'compgen -c hotprobe 2>/dev/null; /bin/rm -f "$HOT_PROBE"; removed=$(compgen -c hotprobe 2>/dev/null); compgen -c lateprobe >/dev/null 2>&1 || :; /bin/mv "$LATE_STAGED" "$LATE_DIRECTORY/lateprobe"; added=$(compgen -c lateprobe 2>/dev/null); printf "removed=%s added=%s\n" "$removed" "$added"'

VANISHED_DIRECTORY="$dir/vanished" PATH="$dir/vanished:/bin" "$BIN" -c \
    'compgen -c vanishprobe >/dev/null 2>&1; /bin/rm -rf "$VANISHED_DIRECTORY"; vanished=$(compgen -c vanishprobe 2>/dev/null); printf "vanished=%s\n" "$vanished"'

/usr/bin/touch -r "$dir/atomic" "$dir/atomic-replacement"
ATOMIC_DIRECTORY="$dir/atomic" ATOMIC_REPLACEMENT="$dir/atomic-replacement" \
    PATH="$dir/atomic:/bin" "$BIN" -c \
    'compgen -c oldatomicprobe >/dev/null 2>&1; /bin/mv "$ATOMIC_DIRECTORY" "$ATOMIC_DIRECTORY-old"; /bin/mv "$ATOMIC_REPLACEMENT" "$ATOMIC_DIRECTORY"; old=$(compgen -c oldatomicprobe 2>/dev/null); new=$(compgen -c newatomicprobe 2>/dev/null); printf "atomic-old=%s atomic-new=%s\n" "$old" "$new"'

CACHE_COMMAND=refreshed CACHE_DIRECTORY="$dir/refresh" \
    CACHE_STAGED="$dir/staged" \
    PATH="$dir/refresh:/bin" "$BIN" -c \
    'command -v "$CACHE_COMMAND" >/dev/null 2>&1; /bin/mv "$CACHE_STAGED" "$CACHE_DIRECTORY/refreshed"; hash -r; "$CACHE_COMMAND"'

CACHE_COMMAND=appeared CACHE_DIRECTORY="$dir/appeared" \
    CACHE_STAGED="$dir/staged-appeared" \
    PATH="$dir/appeared:/bin" "$BIN" -c \
    'command -v "$CACHE_COMMAND" >/dev/null 2>&1; /bin/mv "$CACHE_STAGED" "$CACHE_DIRECTORY/appeared"; "$CACHE_COMMAND"'

analysis_log="$dir/analysis.log"
if "$BIN" -X all -c ':' >/dev/null 2>&1; then
    if ! PATH=/usr/bin:/bin "$BIN" --mood sh -W -X all -c \
        'known_function() { :; }; alias known_alias=:; known_function; known_alias; uname >/dev/null' \
        >/dev/null 2> "$analysis_log"
    then
        exit 1
    fi
    if grep -F "scanning PATH for 'known_function'" "$analysis_log" >/dev/null || \
        grep -F "scanning PATH for 'known_alias'" "$analysis_log" >/dev/null || \
        ! grep -F "scanning PATH for 'uname'" "$analysis_log" >/dev/null
    then
        exit 1
    fi
fi
printf 'analysis-shadowed-no-path-scan\n'

noninteractive_log="$dir/noninteractive.log"
PATH="$dir/one:/bin" "$BIN" -X all -c 'missing_command_xyz' \
    >/dev/null 2>"$noninteractive_log"
if grep -F 'scanning every PATH directory to seed the program cache' \
    "$noninteractive_log" >/dev/null; then
    exit 1
fi
printf 'noninteractive-no-path-index\n'

PATH=/bin "$BIN" -c \
    'compfunc() { :; }; eval "alias compalias=:"; compgen -c shopt 2>/dev/null; compgen -c compfunc 2>/dev/null; compgen -c compalias 2>/dev/null'

if [ "${OS-}" != Windows_NT ]; then
    mkdir "$dir/identity-real"
    /bin/ln -s "$dir/identity-real" "$dir/identity-path"
    IDENTITY_REAL="$dir/identity-real" PATH="$dir/identity-path:/bin" \
        "$BIN" -c '
        compgen -c identity-new >/dev/null 2>&1
        compgen -f "$IDENTITY_REAL/identity-new" >/dev/null 2>&1
        printf "#!/bin/sh\n" > "$IDENTITY_REAL/identity-new"
        /bin/chmod +x "$IDENTITY_REAL/identity-new"
        file_result=$(compgen -f "$IDENTITY_REAL/identity-new" 2>/dev/null)
        [ "$file_result" = "$IDENTITY_REAL/identity-new" ] || exit 1
        printf "identity-alias=%s\n" \
            "$(compgen -c identity-new 2>/dev/null)"
    '

    mkdir "$dir/space path"
    : > "$dir/space path/file name"
    SPACED_DIRECTORY="$dir/space path" "$BIN" -c '
        result=$(compgen -f "$SPACED_DIRECTORY/file" 2>/dev/null)
        [ "$result" = "$SPACED_DIRECTORY/file name" ] && \
            echo compgen-file-literal-path
    '

    mkdir "$dir/mode-change"
    printf '#!/bin/sh\n' > "$dir/mode-change/mode-appeared"
    printf '#!/bin/sh\n' > "$dir/mode-change/mode-vanished"
    chmod +x "$dir/mode-change/mode-vanished"
    MODE_DIRECTORY="$dir/mode-change" PATH="$dir/mode-change:/bin" "$BIN" -c '
        before=$(compgen -c mode-appeared 2>/dev/null)
        /bin/chmod +x "$MODE_DIRECTORY/mode-appeared"
        appeared=$(compgen -c mode-appeared 2>/dev/null)
        /bin/chmod -x "$MODE_DIRECTORY/mode-vanished"
        vanished=$(compgen -c mode-vanished 2>/dev/null)
        printf "mode-before=%s mode-appeared=%s mode-vanished=%s\n" \
            "$before" "$appeared" "$vanished"
    '

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

mkdir "$dir/source-first" "$dir/source-second"
printf 'echo sourced-non-runnable\n' > "$dir/source-first/sourceprobe"
printf '#!/bin/sh\necho wrong-source\n' > "$dir/source-second/sourceprobe"
chmod +x "$dir/source-second/sourceprobe"
PATH="$dir/source-first:$dir/source-second:/bin" "$BIN" -c \
    'source sourceprobe'

mkdir "$dir/all-first" "$dir/all-second"
printf '#!/bin/sh\n' > "$dir/all-first/allprobe"
printf '#!/bin/sh\n' > "$dir/all-second/allprobe"
chmod +x "$dir/all-first/allprobe" "$dir/all-second/allprobe"
PATH="$dir/all-first:$dir/all-first:$dir/all-second:/bin" "$BIN" -c \
    'type -a -p allprobe' | sed "s|$dir|DIR|g"
