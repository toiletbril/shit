#!/bin/bash
# Regression cover for the review fixes whose output bash defines exactly. The
# PIPESTATUS array reports each stage by position, and a negative array index
# counts back from the highest set index across the sparse elements rather than
# the dense length.
true | false | true
echo "${PIPESTATUS[@]}"
a=(A B)
a[5]=z
echo "[${a[-1]}][${a[-2]}]"
b=(x y z)
echo "${b[-1]} ${b[-2]} ${b[-3]}"

true | false | true
printf 'pipeline=%s\n' "${PIPESTATUS[*]}"
true
printf 'single=%s count=%s\n' "${PIPESTATUS[*]}" "${#PIPESTATUS[@]}"
PIPESTATUS=scalar
printf 'scalar=%s count=%s\n' "${PIPESTATUS[*]}" "${#PIPESTATUS[@]}"
PIPESTATUS[8]=sparse
false
printf 'sparse=%s count=%s\n' "${PIPESTATUS[*]}" "${#PIPESTATUS[@]}"
pipe_status_function() { return 7; }
pipe_status_function
printf 'function=%s\n' "${PIPESTATUS[*]}"
substitution_result=$(printf captured)
printf 'substitution=%s result=%s\n' "${PIPESTATUS[*]}" "$substitution_result"

case b in
    ["a"-c]) printf 'quoted-lower-range=match\n' ;;
    *) printf 'quoted-lower-range=miss\n' ;;
esac
case ^ in
    []-a]) printf 'leading-close-range=match\n' ;;
    *) printf 'leading-close-range=miss\n' ;;
esac
