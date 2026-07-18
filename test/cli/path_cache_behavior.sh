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

mkdir "$dir/query-first" "$dir/query-second"
printf '#!/bin/sh\necho compgen-first\n' > "$dir/compgen-staged"
printf '#!/bin/sh\necho compgen-second\n' > \
    "$dir/query-second/compgenprobe"
chmod +x "$dir/compgen-staged" "$dir/query-second/compgenprobe"
QUERY_FIRST="$dir/query-first" QUERY_STAGED="$dir/compgen-staged" \
    PATH="$dir/query-first:$dir/query-second:/bin" "$BIN" -c \
    'compgen -c compgenprobe >/dev/null 2>&1; /bin/mv "$QUERY_STAGED" "$QUERY_FIRST/compgenprobe"; compgenprobe'

printf '#!/bin/sh\necho command-first\n' > "$dir/command-staged"
printf '#!/bin/sh\necho command-second\n' > \
    "$dir/query-second/commandprobe"
chmod +x "$dir/command-staged" "$dir/query-second/commandprobe"
QUERY_FIRST="$dir/query-first" QUERY_STAGED="$dir/command-staged" \
    PATH="$dir/query-first:$dir/query-second:/bin" "$BIN" -c \
    'command -v commandprobe >/dev/null; /bin/mv "$QUERY_STAGED" "$QUERY_FIRST/commandprobe"; commandprobe'

printf '#!/bin/sh\necho type-first\n' > "$dir/type-staged"
printf '#!/bin/sh\necho type-second\n' > "$dir/query-second/typeprobe"
chmod +x "$dir/type-staged" "$dir/query-second/typeprobe"
QUERY_FIRST="$dir/query-first" QUERY_STAGED="$dir/type-staged" \
    PATH="$dir/query-first:$dir/query-second:/bin" "$BIN" -c \
    'type typeprobe >/dev/null; /bin/mv "$QUERY_STAGED" "$QUERY_FIRST/typeprobe"; typeprobe'

printf '#!/bin/sh\necho optimizer-first\n' > "$dir/optimizer-staged"
printf '#!/bin/sh\necho optimizer-second\n' > \
    "$dir/query-second/optimizerprobe"
chmod +x "$dir/optimizer-staged" "$dir/query-second/optimizerprobe"
QUERY_FIRST="$dir/query-first" QUERY_STAGED="$dir/optimizer-staged" \
    PATH="$dir/query-first:$dir/query-second:/bin" "$BIN" -c \
    '/bin/mv "$QUERY_STAGED" "$QUERY_FIRST/optimizerprobe"; optimizerprobe'

printf '#!/bin/sh\necho execution-first\n' > "$dir/execution-staged"
printf '#!/bin/sh\necho execution-second\n' > \
    "$dir/query-second/executionprobe"
chmod +x "$dir/execution-staged" "$dir/query-second/executionprobe"
QUERY_FIRST="$dir/query-first" QUERY_STAGED="$dir/execution-staged" \
    PATH="$dir/query-first:$dir/query-second:/bin" "$BIN" -c \
    'executionprobe; /bin/mv "$QUERY_STAGED" "$QUERY_FIRST/executionprobe"; executionprobe'

printf '#!/bin/sh\necho hash-first\n' > "$dir/hash-staged"
printf '#!/bin/sh\necho hash-second\n' > "$dir/query-second/hashprobe"
chmod +x "$dir/hash-staged" "$dir/query-second/hashprobe"
QUERY_FIRST="$dir/query-first" QUERY_STAGED="$dir/hash-staged" \
    PATH="$dir/query-first:$dir/query-second:/bin" "$BIN" -c \
    'hash hashprobe; /bin/mv "$QUERY_STAGED" "$QUERY_FIRST/hashprobe"; hashprobe'

printf '#!/bin/sh\necho mode-first\n' > "$dir/query-first/modeprobe"
printf '#!/bin/sh\necho mode-second\n' > "$dir/query-second/modeprobe"
chmod +x "$dir/query-second/modeprobe"
QUERY_FIRST="$dir/query-first" \
    PATH="$dir/query-first:$dir/query-second:/bin" "$BIN" -c \
    'compgen -c modeprobe >/dev/null 2>&1; /bin/chmod +x "$QUERY_FIRST/modeprobe"; modeprobe'

mkdir "$dir/query-directory" "$dir/query-directory/directoryprobe"
: > "$dir/query-directory/regularprobe"
PATH="$dir/query-directory:/bin" "$BIN" -c '
    type -p directoryprobe >/dev/null; printf "type-directory=%s " "$?"
    command -v directoryprobe >/dev/null
    printf "command-directory=%s " "$?"
    shitbox which directoryprobe >/dev/null
    printf "which-directory=%s\n" "$?"
    type -p regularprobe >/dev/null; printf "type-regular=%s " "$?"
    command -v regularprobe >/dev/null
    printf "command-regular=%s\n" "$?"
'

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

mkdir "$dir/hash-blocked"
: > "$dir/hash-blocked/hashblocked"
PATH="$dir/hash-blocked:/bin" "$BIN" -c \
    'hash hashblocked >/dev/null 2>&1; printf "hash-blocked=%s\n" "$?"'

mkdir "$dir/source-directory-first" "$dir/source-file-second" \
    "$dir/source-directory-first/source-directory-probe"
printf 'echo source-skipped-directory\n' > \
    "$dir/source-file-second/source-directory-probe"
PATH="$dir/source-directory-first:$dir/source-file-second:/bin" "$BIN" -c \
    'source source-directory-probe'

mkdir "$dir/which-first" "$dir/which-second"
: > "$dir/which-first/whichblocked"
PATH="$dir/which-first:/bin" "$BIN" -c \
    'shitbox which whichblocked >/dev/null 2>&1
    printf "which-blocked=%s\n" "$?"'
printf '#!/bin/sh\necho which-first\n' > "$dir/which-staged"
printf '#!/bin/sh\necho which-second\n' > "$dir/which-second/whichprobe"
chmod +x "$dir/which-staged" "$dir/which-second/whichprobe"
WHICH_FIRST="$dir/which-first" WHICH_STAGED="$dir/which-staged" \
    PATH="$dir/which-first:$dir/which-second:/bin" "$BIN" -c \
    'whichprobe >/dev/null
    /bin/mv "$WHICH_STAGED" "$WHICH_FIRST/whichprobe"
    shitbox which whichprobe' | sed "s|$dir|DIR|g"

mkdir "$dir/command-p-first" "$dir/command-p-second"
printf '#!/bin/sh\necho command-p-first\n' > "$dir/command-p-staged"
printf '#!/bin/sh\necho command-p-second\n' > \
    "$dir/command-p-second/commandpprobe"
chmod +x "$dir/command-p-staged" "$dir/command-p-second/commandpprobe"
COMMAND_P_FIRST="$dir/command-p-first" \
    COMMAND_P_STAGED="$dir/command-p-staged" \
    PATH="$dir/command-p-first:$dir/command-p-second:/bin" "$BIN" -c \
    'commandpprobe
    command -p true
    /bin/mv "$COMMAND_P_STAGED" "$COMMAND_P_FIRST/commandpprobe"
    commandpprobe'

mkdir "$dir/equal-first" "$dir/equal-second"
printf '#!/bin/sh\necho equal-first\n' > "$dir/equal-staged"
printf '#!/bin/sh\necho equal-second\n' > "$dir/equal-second/equalprobe"
chmod +x "$dir/equal-staged" "$dir/equal-second/equalprobe"
EQUAL_FIRST="$dir/equal-first" EQUAL_STAGED="$dir/equal-staged" \
    PATH="$dir/equal-first:$dir/equal-second:/bin" "$BIN" -c \
    'equalprobe
    PATH="$PATH"
    /bin/mv "$EQUAL_STAGED" "$EQUAL_FIRST/equalprobe"
    equalprobe'

mkdir "$dir/mimic-first" "$dir/mimic-second"
printf '#!/bin/sh\necho mimic-first\n' > "$dir/mimic-staged"
printf '#!/bin/sh\necho mimic-second\n' > "$dir/mimic-second/mimicprobe"
chmod +x "$dir/mimic-staged" "$dir/mimic-second/mimicprobe"
printf '#!/bin/bash\nhash -r\n/bin/mv "%s" "%s"\n' \
    "$dir/mimic-staged" "$dir/mimic-first/mimicprobe" > "$dir/mimic-script"
chmod +x "$dir/mimic-script"
MIMIC_SCRIPT="$dir/mimic-script" \
    PATH="$dir/mimic-first:$dir/mimic-second:/bin" "$BIN" -I --mood bash -c \
    'mimicprobe
    "$MIMIC_SCRIPT"
    mimicprobe'

mkdir "$dir/default-path"
printf '#!/bin/sh\n' > "$dir/default-path/onlycustom"
printf '#!/bin/sh\n' > "$dir/default-path/ls"
chmod +x "$dir/default-path/onlycustom" "$dir/default-path/ls"
PATH="$dir/default-path" "$BIN" --mood bash -c \
    'command -p -v onlycustom >/dev/null
    printf "command-p-custom=%s\n" "$?"
    command -p -v ls' | sed "s|^/usr/bin/ls$|DEFAULT-LS|;s|^/bin/ls$|DEFAULT-LS|"

"$BIN" --mood bash -c \
    'command -v missing-command-xyz echo printf
    printf "command-multiple=%s\n" "$?"'

mkdir "$dir/type-all-first" "$dir/type-all-second"
printf '#!/bin/sh\n' > "$dir/type-all-first/typeallprobe"
printf '#!/bin/sh\n' > "$dir/type-all-second/typeallprobe"
chmod +x "$dir/type-all-first/typeallprobe" \
    "$dir/type-all-second/typeallprobe"
PATH="$dir/type-all-first:$dir/type-all-second:/bin" "$BIN" --mood bash -c \
    'type -a -P typeallprobe' | sed "s|$dir|DIR|g"

if [ "${OS-}" != Windows_NT ]; then
    mkdir "$dir/slash-path"
    printf '#!/bin/sh\n' > "$dir/slash-path/slashprobe"
    chmod +x "$dir/slash-path/slashprobe"
    (
        cd "$dir/slash-path" || exit 1
        PATH=/bin "$BIN" --mood bash -c '
            type -p ./slashprobe
            type -P ./slashprobe
            shitbox which ./slashprobe
            hash ./slashprobe
            printf "hash-slash=%s\n" "$?"
            hash ./missing
            printf "hash-missing-slash=%s\n" "$?"
        '
    )
fi
