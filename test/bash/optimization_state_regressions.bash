#!/bin/bash

subshell_return() {
    (return 7)
    printf 'after-return=%s\n' "$?"
}
subshell_return
printf 'function-return=%s\n' "$?"

indexed=(one two)
associative=global
declare -A mapping=([key]=parent)
(
    indexed[0]=changed
    indexed[1]=leaked
    mapping[key]=child
    mapping[extra]=child
)
printf 'indexed=%s,%s\n' "${indexed[0]}" "${indexed[1]}"
printf 'mapping=%s,%s\n' "${mapping[key]}" "${mapping[extra]-missing}"

case c in
    [c-a]) echo reversed-range-match ;;
    *) echo reversed-range-miss ;;
esac
case - in
    [a-b-c]) echo hyphen-match ;;
    *) echo hyphen-miss ;;
esac

declare -A churn
churn_index=0
while [ "$churn_index" -lt 24 ]; do
    churn["key_$churn_index"]=$churn_index
    churn_index=$((churn_index + 1))
done
churn_index=0
while [ "$churn_index" -lt 1000 ]; do
    unset 'churn[key_12]'
    churn[key_12]=12
    churn_index=$((churn_index + 1))
done
printf 'churn-count=%s churn-value=%s\n' "${#churn[@]}" "${churn[key_12]}"

collision_name_14=14
collision_name_24=24
collision_name_34=34
collision_name_44=44
collision_name_54=54
collision_name_64=64
collision_name_74=74
collision_name_84=84
collision_name_94=94
unset collision_name_24 collision_name_44 collision_name_64 collision_name_84
collision_name_11009=11009
collision_name_11019=11019
(
    collision_name_14=child
    unset collision_name_34
)
printf 'collisions=%s,%s,%s,%s,%s,%s,%s\n' \
    "$collision_name_14" "$collision_name_34" "$collision_name_54" \
    "$collision_name_74" "$collision_name_94" \
    "$collision_name_11009" "$collision_name_11019"

reuse_deep_frame() {
    local frame_value=$1
    if [ "$1" -gt 0 ]; then
        reuse_deep_frame "$((frame_value - 1))"
    else
        printf 'deep-frame=%s\n' "$frame_value"
    fi
}
reuse_shallow_frame() {
    local frame_value=$1
    printf 'shallow-frame=%s\n' "$frame_value"
}
reuse_deep_frame 12
reuse_shallow_frame first
reuse_shallow_frame second

export inherited_local=environment
indexed_local=(zero one)
local_lookup_probe() {
    local inherited_local ordinary_unset_local IFS indexed_local
    printf 'local-lookups=%s,%s,%s,%s\n' "$inherited_local" \
        "${ordinary_unset_local-unset}" "${#IFS}" "$indexed_local"
}
local_lookup_probe

eval 'eval_defined_function() { printf "eval-function=%s\\n" "$1"; }'
eval_defined_function alive
source_file=$(mktemp)
trap '[ -n "$source_file" ] && /bin/rm -f "$source_file"' EXIT
printf 'source_defined_function() { printf "source-function=%s\\n" "$1"; }\n' \
    > "$source_file"
source "$source_file"
/bin/rm -f "$source_file"
source_file=
source_defined_function alive
substitution_source='printf substitution'
printf 'substitution-reparse=%s,%s\n' "$($substitution_source)" \
    "$($substitution_source)"

positional_parameter_substitution() {
    printf 'positional-before=%s\n' "$1"
    nested_value=$(true)
    printf 'positional-after=%s\n' "$1"
}
positional_parameter_substitution alive

persistent_substitution_cache() {
    printf 'persistent-cache=%s\n' "$(printf alive)"
}
persistent_substitution_cache
persistent_substitution_cache
