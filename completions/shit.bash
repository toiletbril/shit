#!/bin/bash
# bash completion for the shit shell.
#
# Do:
# mv shit.bash /usr/share/bash-completion/completions/shit

_shit_complete()
{
    local current_word previous_word
    current_word=${COMP_WORDS[COMP_CWORD]}
    previous_word=${COMP_WORDS[COMP_CWORD-1]}

    local mood_values="shit bash sh bash-posix"
    local log_levels="info debug all"

    case $previous_word in
        -M|--mood)
            COMPREPLY=( $(compgen -W "$mood_values" -- "$current_word") )
            return
            ;;
        -L|--init-moods)
            COMPREPLY=( $(compgen -W "$mood_values" -- "$current_word") )
            return
            ;;
        -X|--debug-logging)
            COMPREPLY=( $(compgen -W "$log_levels" -- "$current_word") )
            return
            ;;
        --rcfile|--debug-logging-file)
            COMPREPLY=( $(compgen -f -- "$current_word") )
            return
            ;;
    esac

    local long_flags="--version --short-version --help --interactive --stdin \
--command --error-exit --no-glob --one-command --verbose --xtrace --export-all \
--no-clobber --no-exec --no-unset --login --rcfile --privileged --clean --posix --mood \
--init-moods --mimicry --dumb --list-diagnostics \
--no-diagnostics --no-init-diagnostics --no-completion --no-syntax-highlighting \
--enable-shitbox \
--show-ast \
--show-optimizer-state --show-exit-code --show-lexed-words --show-stats --show-memory \
--debug-logging --debug-logging-file"

    local short_flags="-V -i -s -c -e -f -t -v -x -a -C -n -u -l -p -M -L -I -W \
-T -A -E -R -X"

    if [[ $current_word == --* ]]; then
        COMPREPLY=( $(compgen -W "$long_flags" -- "$current_word") )
    elif [[ $current_word == -* ]]; then
        COMPREPLY=( $(compgen -W "$short_flags $long_flags" -- "$current_word") )
    else
        COMPREPLY=( $(compgen -f -- "$current_word") )
    fi
}

complete -o filenames -F _shit_complete shit

_shitbox_utils="basename calc cat cp dirname du env find grep head killall ln \
ls make mkdir mv pkill ps realpath rm rmdir seq sleep sort tail tee timeout touch tr \
uniq unlink wc which whoami yes"

_shitbox_util_flags()
{
    case $1 in
        ls)            echo "-a -1 -l -h" ;;
        ln)            echo "-s -f" ;;
        rm)            echo "-r -R -f" ;;
        mkdir)         echo "-p" ;;
        cp)            echo "-r -R -v" ;;
        mv)            echo "-f -v" ;;
        cat)           echo "-n" ;;
        tee)           echo "-a" ;;
        touch)         echo "-c" ;;
        du)            echo "-s -h" ;;
        head|tail)     echo "-n" ;;
        wc)            echo "-l -w -c" ;;
        tr)            echo "-d" ;;
        grep)          echo "-i -v" ;;
        sort)          echo "-r" ;;
        uniq)          echo "-c" ;;
        timeout)       echo "-s --signal -k --kill-after -p --preserve-status" ;;
        pkill|killall) echo "-s -l" ;;
        make)          echo "-f" ;;
        find)          echo "-name -type -maxdepth -mindepth -print" ;;
        *)             echo "" ;;
    esac
}

_shitbox_complete()
{
    local current_word
    current_word=${COMP_WORDS[COMP_CWORD]}

    if [[ $COMP_CWORD -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "$_shitbox_utils --list --assimilate --help" \
            -- "$current_word") )
        return
    fi

    local util=${COMP_WORDS[1]}
    if [[ $current_word == -* ]]; then
        COMPREPLY=( $(compgen -W "$(_shitbox_util_flags "$util")" -- "$current_word") )
    else
        COMPREPLY=( $(compgen -f -- "$current_word") )
    fi
}

complete -o filenames -F _shitbox_complete shitbox
