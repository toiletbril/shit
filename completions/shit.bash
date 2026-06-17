# bash completion for the shit shell.
#
# Source it from ~/.bashrc, or drop it into a bash-completion directory such as
# /usr/share/bash-completion/completions/shit, so completing an argument to the
# shit command offers its flags and the mood values. It loads under bash and
# under shit's bash mood, which reads the bash completion.

_shit_complete()
{
    local current_word previous_word
    current_word=${COMP_WORDS[COMP_CWORD]}
    previous_word=${COMP_WORDS[COMP_CWORD-1]}

    local mood_values="shit bash sh"
    local log_levels="info debug all"

    # A flag that takes a value completes that value rather than another flag.
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
--no-clobber --no-exec --no-unset --login --rcfile --privileged --mood \
--init-moods --mimicry --dumb --force-warnings --list-diagnostics \
--no-diagnostics --no-init-diagnostics --no-completion --enable-shitbox \
--show-ast \
--show-optimizer-state --show-exit-code --show-lexed-words --show-stats --show-memory \
--debug-logging --debug-logging-file"

    local short_flags="-V -i -s -c -e -f -t -v -x -a -C -n -u -l -p -M -L -I -W \
-T -A -E -R -X"

    # A leading dash asks for a flag, anything else completes a file the way a
    # script or operand reads.
    if [[ $current_word == --* ]]; then
        COMPREPLY=( $(compgen -W "$long_flags" -- "$current_word") )
    elif [[ $current_word == -* ]]; then
        COMPREPLY=( $(compgen -W "$short_flags $long_flags" -- "$current_word") )
    else
        COMPREPLY=( $(compgen -f -- "$current_word") )
    fi
}

complete -o filenames -F _shit_complete shit

# Completion for the shitbox builtin. Completing the first argument offers the
# utility names, and completing a later argument offers that utility's flags or
# a file. The bare utility names are left to the system completions, since this
# script runs under a normal bash where ls and the rest are the real tools.
_shitbox_utils="basename cat cp dirname du env find grep head killall ln \
ls make mkdir mv pkill ps realpath rm rmdir seq sleep sort tail tee touch tr \
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
        pkill|killall) echo "-s" ;;
        make)          echo "-f" ;;
        find)          echo "-name -type -maxdepth -mindepth -print" ;;
        *)             echo "" ;;
    esac
}

_shitbox_complete()
{
    local current_word
    current_word=${COMP_WORDS[COMP_CWORD]}

    # The first argument names the utility to run.
    if [[ $COMP_CWORD -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "$_shitbox_utils" -- "$current_word") )
        return
    fi

    # A later argument completes the chosen utility's flags or a file.
    local util=${COMP_WORDS[1]}
    if [[ $current_word == -* ]]; then
        COMPREPLY=( $(compgen -W "$(_shitbox_util_flags "$util")" -- "$current_word") )
    else
        COMPREPLY=( $(compgen -f -- "$current_word") )
    fi
}

complete -o filenames -F _shitbox_complete shitbox
