d=$(mktemp -d)
trap 'test -n "$d" && /bin/rm -rf "$d"' EXIT

detector=../scripts/cpu-count.sh

NUMBER_OF_PROCESSORS=12 PATH="$d" "$detector"

printf '#!/bin/sh\nprintf "6\\n"\n' > "$d/nproc"
chmod +x "$d/nproc"
PATH="$d" "$detector"

/bin/rm -f "$d/nproc"
printf '#!/bin/sh\nprintf "8\\n"\n' > "$d/sysctl"
chmod +x "$d/sysctl"
PATH="$d" "$detector"

/bin/rm -f "$d/sysctl"
printf '#!/bin/sh\nprintf "4\\n"\n' > "$d/getconf"
chmod +x "$d/getconf"
PATH="$d" "$detector"

/bin/rm -f "$d/getconf"
PATH="$d" "$detector"
