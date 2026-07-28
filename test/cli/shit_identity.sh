#!/bin/sh

directory=$(mktemp -d)
trap '[ -n "$directory" ] && rm -rf "$directory"' EXIT HUP INT TERM
export IDENTITY_TEST_DIRECTORY=$directory
if [ "${OS-}" = Windows_NT ]; then
    "$BIN" -c 'shitbox cp "$1" "$2"' \
        test-copy "$BIN" "$directory/entry.exe"
    identity_entry=$directory/entry.exe
    relative_identity_entry=./entry.exe
else
    ln -s "$BIN" "$directory/target"
    ln -s target "$directory/middle"
    ln -s middle "$directory/entry"
    identity_entry=$directory/entry
    relative_identity_entry=./entry
fi

validate_identity()
{
    "$@" -c '
        cd "$IDENTITY_TEST_DIRECTORY" || exit 1
        case $SHIT_IDENTITY in
            *[!0-9a-f]*|"") exit 1 ;;
            *) [ "${#SHIT_IDENTITY}" -eq 64 ] ;;
        esac
    '
}

validate_identity "$identity_entry" || exit 1
(cd "$directory" && validate_identity "$relative_identity_entry") || exit 1
if [ "${OS-}" != Windows_NT ]; then
    (PATH="$directory${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH"
     validate_identity entry) || exit 1
fi

SHIT_IDENTITY=forged "$BIN" -c '
    identity=$SHIT_IDENTITY
    case $identity in
        *[!0-9a-f]*|"") valid=0 ;;
        *) [ "${#identity}" -eq 64 ] && valid=1 || valid=0 ;;
    esac
    shitbox env > "$IDENTITY_TEST_DIRECTORY/environment-output"
    shitbox grep "^SHIT_IDENTITY=$identity$" \
        "$IDENTITY_TEST_DIRECTORY/environment-output" >/dev/null &&
        exported=1 || exported=0
    readonly -p > "$IDENTITY_TEST_DIRECTORY/readonly-output"
    shitbox grep "^readonly SHIT_IDENTITY=" \
        "$IDENTITY_TEST_DIRECTORY/readonly-output" \
        > "$IDENTITY_TEST_DIRECTORY/identity-output" &&
        readonly_status=1 || readonly_status=0
    printf "valid=%s exported=%s readonly=%s\n" \
        "$valid" "$exported" "$readonly_status"
'

if [ "${OS-}" = Windows_NT ]; then
    materialized_environment=$(SHIT_IDENTITY=forged "$BIN" -c \
        'shitbox timeout 1 cmd.exe /d /c set')
else
    materialized_environment=$(SHIT_IDENTITY=forged "$BIN" -c \
        'shitbox timeout 1 /usr/bin/env')
fi
printf '%s\n' "$materialized_environment" |
    grep '^SHIT_IDENTITY=' |
    sed 's/=.*/=materialized/'
