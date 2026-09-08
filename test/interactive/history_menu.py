#!/usr/bin/env python3
"""Ctrl-R opens the selectable history menu under the prompt.

The menu is drawn by the vendored editor in the rows below the input block, so
every check reads the raw transcript of a pty session. The interactive selector
is chosen with --tab-selector, since the incremental search is what a session
gets without it.
"""

import fcntl
import os
import pty
import select
import signal
import struct
import sys
import tempfile
import termios
import time

binary = os.path.abspath(sys.argv[1])

SELECTED_SGR = b"\x1b[7m"
DIMMED_SGR = b"\x1b[90m"
HIGHLIGHT_RESET = b"\x1b[0m"

# The word each command prints is an operand. A menu row never carries the
# angle bracketed output that proves the entry ran.
SEEDED_COMMANDS = (
    "printf '<%s>\\n' one",
    "printf '<%s>\\n' two",
    "printf '<%s>\\n' three",
)


def read_until_idle(master, timeout, required_output=None):
    output = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        readable, _, _ = select.select([master], [], [], 0.1)
        if master not in readable:
            if output and (required_output is None or required_output in output):
                break
            continue
        try:
            chunk = os.read(master, 4096)
        except OSError:
            break
        if not chunk:
            break
        output += chunk
    return output


def run_history_menu(directory, typed, keys, rows=24):
    """Seed the history, type the words, press ctrl-R, and send the keys.

    The transcript is split at ctrl-R. A check can tell what the menu drew from
    what the accepted line printed.
    """
    pid, master = pty.fork()
    if pid == 0:
        fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", rows, 120, 0, 0))
        os.environ["TERM"] = "xterm-256color"
        os.environ["HOME"] = directory
        os.environ["KOSH_HISTORY"] = os.path.join(directory, "history")
        os.chdir(directory)
        os.execv(
            binary,
            [binary, "--norc", "--no-diagnostics", "--tab-selector",
             "interactive"],
        )

    read_until_idle(master, 3)
    for command in SEEDED_COMMANDS:
        os.write(master, command.encode() + b"\n")
        read_until_idle(master, 2)

    if typed:
        os.write(master, typed.encode())
        read_until_idle(master, 1)

    os.write(master, b"\x12")
    menu = read_until_idle(master, 2)

    for key in keys:
        os.write(master, key)
        menu += read_until_idle(master, 1)

    # Accepting a row only rewrites the line. The run needs a submit of its
    # own.
    os.write(master, b"\n")
    menu += read_until_idle(master, 2)
    os.write(master, b"printf 'MARKER-%s\\n' END\nexit\n")
    tail = read_until_idle(master, 2)
    os.close(master)

    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        waited, _ = os.waitpid(pid, os.WNOHANG)
        if waited == pid:
            break
        time.sleep(0.02)
    else:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)

    return menu, tail


def main():
    with tempfile.TemporaryDirectory() as directory:
        opened, _ = run_history_menu(directory, "", [])
        menu_lists_every_entry = (
            b"' one" in opened and b"' two" in opened and b"' three" in opened
        )
        menu_opens_with_first_selection = SELECTED_SGR in opened
        # The dimmed first row names the source and the keys it answers.
        help_row_names_the_source = (
            b"  " + DIMMED_SGR + b"incremental history search. enter to accept"
            in opened
        )

        # The newest entry heads the list. The highlight opens on the last
        # command the session ran.
        selected_start = opened.find(SELECTED_SGR)
        selected_end = opened.find(HIGHLIGHT_RESET, selected_start)
        selected_text = opened[selected_start + len(SELECTED_SGR):selected_end]
        the_newest_entry_is_selected = (
            selected_start >= 0
            and selected_end >= 0
            and selected_text == b"  printf '<%s>\\n' three  "
        )

        accepted, marker = run_history_menu(directory, "", [b"\n"])
        enter_runs_the_highlighted_entry = b"<three>" in accepted

        # Typing narrows the list. The Enter that follows is answered by the
        # menu. A closed menu would submit the letters on their own.
        filtered, _ = run_history_menu(directory, "", [b"o", b"n", b"e", b"\n"])
        typing_narrows_the_list = b"<one>" in filtered

        # Backspace widens the list back to every entry. The newest command
        # heads it again.
        widened, _ = run_history_menu(
            directory, "", [b"o", b"n", b"e", b"\x7f", b"\x7f", b"\x7f", b"\n"]
        )
        backspace_widens_the_list = b"<three>" in widened

        # A search that matches nothing keeps the menu open on the row that says
        # so, and the erase that follows brings the list back for the Enter.
        emptied, _ = run_history_menu(
            directory, "", [b"z", b"z", b"\x7f", b"\x7f", b"\n"]
        )
        an_empty_search_keeps_the_menu_open = b"no matches" in emptied
        an_erase_recovers_the_list = b"<three>" in emptied

        # The down arrow reaches the entry below the newest one.
        moved, _ = run_history_menu(directory, "", [b"\x1b[B", b"\n"])
        a_movement_key_reaches_an_older_entry = b"<two>" in moved

        # Escape puts back the line the menu opened on. That line is empty here.
        # The command typed afterwards runs on its own.
        cancelled, _ = run_history_menu(
            directory, "", [b"\x1b", b"printf '<%s>\\n' kept", b"\n"]
        )
        escape_leaves_the_line_alone = b"<kept>" in cancelled

        # A line the history cannot match leaves the key with nothing to show,
        # so the line survives ctrl-R untouched.
        unmatched, _ = run_history_menu(
            directory, "printf '<%s>\\n' zzz", [b"\n"]
        )
        an_unmatched_line_survives = b"<zzz>" in unmatched

        prompt_stays_usable = b"MARKER-END" in marker

        results = {
            "MENU_LISTS_EVERY_ENTRY": menu_lists_every_entry,
            "MENU_OPENS_WITH_FIRST_SELECTION": menu_opens_with_first_selection,
            "HELP_ROW_NAMES_THE_SOURCE": help_row_names_the_source,
            "THE_NEWEST_ENTRY_IS_SELECTED": the_newest_entry_is_selected,
            "ENTER_RUNS_THE_HIGHLIGHTED_ENTRY": (
                enter_runs_the_highlighted_entry
            ),
            "TYPING_NARROWS_THE_LIST": typing_narrows_the_list,
            "BACKSPACE_WIDENS_THE_LIST": backspace_widens_the_list,
            "AN_EMPTY_SEARCH_KEEPS_THE_MENU_OPEN": (
                an_empty_search_keeps_the_menu_open
            ),
            "AN_ERASE_RECOVERS_THE_LIST": an_erase_recovers_the_list,
            "A_MOVEMENT_KEY_REACHES_AN_OLDER_ENTRY": (
                a_movement_key_reaches_an_older_entry
            ),
            "ESCAPE_LEAVES_THE_LINE_ALONE": escape_leaves_the_line_alone,
            "AN_UNMATCHED_LINE_SURVIVES": an_unmatched_line_survives,
            "PROMPT_STAYS_USABLE": prompt_stays_usable,
        }

    passed = all(results.values())
    for name, value in results.items():
        print(f"{name}: {value}")
    print("HISTORY_MENU:", passed)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
