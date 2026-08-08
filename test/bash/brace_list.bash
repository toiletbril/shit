#!/bin/bash
# Bash brace expansion of the comma-list form {a,b,c}, checked byte-for-byte
# against bash. Covers a preamble and postamble, multiple groups in a cartesian
# product, nesting, empty alternatives, a variable in an alternative, and the
# non-group cases that stay literal.
echo {a,b,c}
echo pre{a,b,c}post
echo {a,b}{1,2}
echo a{b,c}d{e,f}
echo {a,{b,c},d}
echo file{1,2,3}.txt
echo {a,b,}
echo {,a,b}
v=hi
echo {$v,x}
echo "{a,b}"
echo '{a,b}'
echo {a}
echo {}
echo path/{src,test}/main


# Sequence ranges with a step and zero-padding, including descending order, a
# negative step, a wider pad than the value, and degenerate single-value ranges,
# checked byte-for-byte against bash.
echo {001..10}
echo {a..z..3}
echo {z..a..5}
echo {10..1..2}
echo {-5..5..2}
echo {00..05}
echo {1..10..-2}
echo {10..1..-2}
echo {1..1}
echo {a..a}
echo {0..-5}

# Bash brace sequence expansion {m..n} and {m..n..s}, numeric and alphabetic,
# checked byte-for-byte against bash. Covers ascending and descending, a step,
# zero-padding, letters, a preamble and postamble, and the cartesian product.
echo {1..5}
echo {5..1}
echo {1..10..2}
echo {0..20..5}
echo {a..e}
echo {e..a}
echo {A..F}
echo {01..05}
echo {1..3}{a..c}
echo pre{1..3}post
echo {-3..3}
echo {a..e..2}
echo file{1..3}.txt
echo {10..1..3}
echo x{1..2}y{3..4}z
echo {9223372036854775807..9223372036854775807}
echo {-9223372036854775808..-9223372036854775808}

brace_command='printf "opaque=%s\\n" {'
brace_index=0
while [ "$brace_index" -lt 300 ]; do
    [ "$brace_index" -eq 0 ] || brace_command="$brace_command,"
    eval "opaque_$brace_index=$brace_index"
    brace_command="$brace_command"'${opaque_'"$brace_index"'}'
    brace_index=$((brace_index + 1))
done
brace_command="$brace_command}"
eval "$brace_command"

# Escaped and quoted braces that stay literal, and an escaped comma inside a
# group, checked byte-for-byte against bash. A backslash before a brace or a
# comma removes its special meaning, while a quote keeps the whole word literal.
echo \{a,b}
echo \{a,b\}
echo {a\,b,c}
echo {a,b\,c}
echo a{b,c\}
echo "{a,b}"
echo '{a,b}'
echo a{}b
echo a{,}b

# Nested groups and the cartesian product of several groups in one word,
# including a range crossed with a list, checked byte-for-byte against bash.
echo {{a,b},{c,d}}
echo {a,b}{c,d}{e,f}
echo {1..3}{x,y}
echo {a,b}{1..2}
echo {a..c}{1..2}
echo a{b,{c,d},e}f
echo x{1..2}{a,b}z

# Brace forms that bash leaves literal because the endpoints do not form a valid
# range, a number against a letter or a non-integer endpoint, checked
# byte-for-byte against bash. A single mixed list still expands.
echo {1,a}
echo {1..a}
echo {a..1}
echo {.5..5}
echo {1.5..5}
echo {a,b,c,}
echo {a,b,}
echo {,a,b}

# Brace expansion feeding command arguments and path-shaped words, checked
# byte-for-byte against bash. The expansion happens before the command sees its
# arguments, so printf and echo receive the already expanded list.
printf '%s\n' file{1,2,3}.txt
echo backup.{tar,tar.gz}
echo img{01..03}.png
echo path/{src,test}/main
echo {a..c}/{x,y}
printf '[%s]' {1..4}
echo

for w in function time; do echo "list: $w"; done
for for in for; do echo "name: $for"; done
for x in do done if then else fi case esac while until; do echo "kw: $x"; done
for time in while function; do echo "both: $time"; done
select s in if then in; do echo "sel: $s"; break; done <<< 1
