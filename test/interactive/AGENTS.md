Interactive pty harnesses
=========================

These checks cover behavior that needs a controlling terminal.

Build with `make MODE=dbg`, then run a script with an optional binary path. The
main test suite discovers every `interactive/*.py` script when Python is
available.

`calc_history.py` checks that calc expressions stay out of the shell history.
`cat_syntax_highlighting.py` checks terminal syntax colors for koshkit cat.
`completion_menu.py` checks the selectable candidate menu a second tab opens.
`format_syntax_highlighting.py` checks terminal syntax colors for formatted
source.
`fzf_completion.py` checks the interactive completion selector against a stub,
so it needs no fzf installed.
`history_load_failure.py` checks that a session names an unreadable history
file at startup.
`history_menu.py` checks the selectable history menu ctrl-R opens under the
interactive selector.
`history_search.py` checks the incremental history search ctrl-R opens under
the plain selector.
`long_warning_window.py` checks clipped diagnostics and caret alignment.
`mimic_terminal_handoff.py` checks foreground handoff and prompt recovery.
`read_silent.py` checks that the read builtin hides a silent operand.
`underline_term_support.py` checks terminal underline capability handling.
