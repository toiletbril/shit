#!/bin/sh

SHIT_IDENTITY=forged "$BIN" -c '
    identity=$SHIT_IDENTITY
    case $identity in
        *[!0-9a-f]*|"") valid=0 ;;
        *) [ "${#identity}" -eq 64 ] && valid=1 || valid=0 ;;
    esac
    external=$(/usr/bin/env | shitbox grep "^SHIT_IDENTITY=")
    [ "$external" = "SHIT_IDENTITY=$identity" ] &&
        exported=1 || exported=0
    readonly -p | shitbox grep "^readonly SHIT_IDENTITY=" >/dev/null &&
        readonly_status=1 || readonly_status=0
    printf "valid=%s exported=%s readonly=%s\n" \
        "$valid" "$exported" "$readonly_status"
'

SHIT_IDENTITY=forged "$BIN" -c \
    'shitbox timeout 1 /usr/bin/env' |
    grep '^SHIT_IDENTITY=' |
    sed 's/=.*/=materialized/'
