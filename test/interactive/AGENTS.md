Interactive pty harnesses
=========================

These checks cover behavior that needs a controlling terminal.

Build with `make MODE=dbg`, then run a script with an optional binary path. The
main test suite discovers every `interactive/*.py` script when Python is
available.

`cat_syntax_highlighting.py` checks terminal syntax colors for koshkit cat.
`completion_menu.py` checks the selectable candidate menu a second tab opens.
`format_syntax_highlighting.py` checks terminal syntax colors for formatted
source.
`fzf_completion.py` checks the interactive completion selector against a stub,
so it needs no fzf installed.
`history_load_failure.py` checks that a session names an unreadable history
file at startup.
`long_warning_window.py` checks clipped diagnostics and caret alignment.
`mimic_terminal_handoff.py` checks foreground handoff and prompt recovery.
`underline_term_support.py` checks terminal underline capability handling.
