#!/bin/bash
for w in function time; do echo "list: $w"; done
for for in for; do echo "name: $for"; done
for x in do done if then else fi case esac while until; do echo "kw: $x"; done
for time in while function; do echo "both: $time"; done
select s in if then in; do echo "sel: $s"; break; done <<< 1
