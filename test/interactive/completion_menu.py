#!/usr/bin/env python3
"""A second tab opens the selectable candidate menu under the prompt.

The menu is drawn by the vendored editor in the rows below the input block, so
every check reads the raw transcript of a pty session. The interactive selector
is chosen with --tab-selector, since the plain listing is what a session gets
without it.
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

# Each scenario runs from a candidate tree, so the binary is resolved before any
# directory change.
binary = os.path.abspath(sys.argv[1])

SELECTED_SGR = b"\x1b[7m"


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


def run_menu(directory, tree, typed, keys, rows=24):
    """Type the words, press tab twice, send the keys, and submit the line.

    The transcript is split at the second tab, so a check can tell what the menu
    drew from what the accepted line printed.
    """
    pid, master = pty.fork()
    if pid == 0:
        fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", rows, 120, 0, 0))
        os.environ["TERM"] = "xterm-256color"
        os.environ["HOME"] = directory
        os.environ["KOSH_HISTORY"] = os.path.join(directory, "history")
        os.chdir(os.path.join(directory, tree))
        os.execv(
            binary,
            [binary, "--norc", "--no-diagnostics", "--tab-selector",
             "interactive"],
        )

    read_until_idle(master, 3)
    os.write(master, typed.encode())
    read_until_idle(master, 1)

    # The first tab only inserts the common prefix, so the menu belongs to the
    # second one.
    os.write(master, b"\t")
    read_until_idle(master, 2)

    os.write(master, b"\t")
    menu = read_until_idle(master, 2)

    for key in keys:
        os.write(master, key)
        menu += read_until_idle(master, 1)

    os.write(master, b"\n")
    tail = read_until_idle(master, 2, b"MARKER-END")
    os.write(master, b"printf 'MARKER-END\\n'\nexit\n")
    tail += read_until_idle(master, 2)
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
        tree = os.path.join(directory, "tree")
        os.mkdir(tree)
        for name in ("alpha-one", "alpha-two", "alpha-three"):
            open(os.path.join(tree, name), "w").close()

        tall = os.path.join(directory, "tall")
        os.mkdir(tall)
        for index in range(1, 9):
            open(os.path.join(tall, "menu-%d" % index), "w").close()

        typed = "printf '<%s>\\n' alpha"
        tall_typed = "printf '<%s>\\n' menu"

        opened, _ = run_menu(directory, "tree", typed, [])
        menu_lists_every_candidate = (
            b"alpha-one" in opened
            and b"alpha-two" in opened
            and b"alpha-three" in opened
        )
        # Nothing is highlighted until a movement key asks for a selection, so
        # the line stays exactly where the prefix left it.
        menu_opens_without_a_selection = SELECTED_SGR not in opened

        moved, accepted = run_menu(directory, "tree", typed, [b"\x1b[B", b"\n"])
        a_movement_key_highlights_a_row = SELECTED_SGR in moved
        enter_accepts_the_highlighted_row = (
            b"<alpha-one>" in accepted
            or b"<alpha-two>" in accepted
            or b"<alpha-three>" in accepted
        )

        tabbed, tab_accepted = run_menu(
            directory, "tree", typed, [b"\t", b"\n"]
        )
        tab_also_steps_forward = SELECTED_SGR in tabbed
        tab_selection_is_accepted = (
            b"<alpha-one>" in tab_accepted
            or b"<alpha-two>" in tab_accepted
            or b"<alpha-three>" in tab_accepted
        )

        _, dismissed = run_menu(directory, "tree", typed, [b"\x1b"])
        escape_leaves_the_line_alone = b"<alpha->" in dismissed

        _, typed_through = run_menu(directory, "tree", typed, [b"x"])
        an_ordinary_key_reaches_the_line = b"<alpha-x>" in typed_through

        # Eight candidates in an eight row terminal cannot all be shown, so the
        # menu bounds its rows and names the part it drew.
        bounded, _ = run_menu(directory, "tall", tall_typed, [], rows=8)
        a_long_list_is_bounded = b"showing 1-" in bounded and b" of 8" in bounded
        the_first_candidate_is_visible = b"menu-1" in bounded

        prompt_stays_usable = b"MARKER-END" in accepted

        results = {
            "MENU_LISTS_EVERY_CANDIDATE": menu_lists_every_candidate,
            "MENU_OPENS_WITHOUT_A_SELECTION": menu_opens_without_a_selection,
            "A_MOVEMENT_KEY_HIGHLIGHTS_A_ROW": a_movement_key_highlights_a_row,
            "ENTER_ACCEPTS_THE_HIGHLIGHTED_ROW": (
                enter_accepts_the_highlighted_row
            ),
            "TAB_ALSO_STEPS_FORWARD": tab_also_steps_forward,
            "TAB_SELECTION_IS_ACCEPTED": tab_selection_is_accepted,
            "ESCAPE_LEAVES_THE_LINE_ALONE": escape_leaves_the_line_alone,
            "AN_ORDINARY_KEY_REACHES_THE_LINE": (
                an_ordinary_key_reaches_the_line
            ),
            "A_LONG_LIST_IS_BOUNDED": a_long_list_is_bounded,
            "THE_FIRST_CANDIDATE_IS_VISIBLE": the_first_candidate_is_visible,
            "PROMPT_STAYS_USABLE": prompt_stays_usable,
        }

    passed = all(results.values())
    for name, value in results.items():
        print(f"{name}: {value}")
    print("COMPLETION_MENU:", passed)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
