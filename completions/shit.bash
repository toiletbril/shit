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
--no-diagnostics --no-init-diagnostics --no-completion --show-ast \
--debug-optimizer --show-exit-code --show-lexed-words --show-stats --show-memory \
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
