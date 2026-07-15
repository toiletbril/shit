d=$(mktemp -d)
trap 'test -n "$d" && /bin/rm -rf "$d"' EXIT

detector=../scripts/cpu-count.sh

NUMBER_OF_PROCESSORS=12 PATH="$d" "$detector"

printf '#!/bin/sh\n[ "$1" = --all ] || exit 1\nprintf "6\\n"\n' > "$d/nproc"
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

default_jobs=$(cd .. && make -n CPU_COUNT=6 shit 2>/dev/null)
case "$default_jobs" in
    *'-j6 -C src shit'*) echo default-make-uses-all ;;
    *) echo default-make-missed ;;
esac

variable_jobs=$(cd .. && make -n CPU_COUNT=6 LABEL=project shit 2>/dev/null)
case "$variable_jobs" in
    *'-j6 -C src shit'*) echo variable-make-uses-all ;;
    *) echo variable-make-missed ;;
esac

explicit_jobs=$(cd .. && make -n -j2 CPU_COUNT=6 shit 2>/dev/null)
case "$explicit_jobs" in
    *'-j6 -C src shit'*) echo explicit-make-overridden ;;
    *) echo explicit-make-preserved ;;
esac

single_job=$(cd .. && make -n -j1 CPU_COUNT=6 shit 2>/dev/null)
case "$single_job" in
    *'-j6 -C src shit'*) echo single-make-overridden ;;
    *) echo single-make-preserved ;;
esac

windows_jobs=$(cd .. && OS=Windows_NT NUMBER_OF_PROCESSORS=7 \
    make -n shit 2>/dev/null)
case "$windows_jobs" in
    *'-j7 -C src shit'*) echo windows-make-uses-all ;;
    *) echo windows-make-missed ;;
esac

for invalid_count in 0 invalid; do
    windows_jobs=$(cd .. && OS=Windows_NT \
        NUMBER_OF_PROCESSORS=$invalid_count make -n shit 2>/dev/null)
    case "$windows_jobs" in
        *"-j$invalid_count -C src shit"*)
            echo windows-invalid-count-leaked
            exit 1
            ;;
    esac
done
echo windows-invalid-count-clean

if [ -e ../src/NUL ]; then
    echo windows-dry-run-leaked
else
    echo windows-dry-run-clean
fi

default_targets=$(cd .. && make -n MODE=dbg CPU_COUNT=1 2>/dev/null)
case "$default_targets" in
    *'Launching tests'*) echo default-make-launched-tests ;;
    *) echo default-make-builds-shell ;;
esac
