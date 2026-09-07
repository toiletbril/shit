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
HIGHLIGHT_RESET = b"\x1b[0m"


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


def run_menu(
    directory,
    tree,
    typed,
    keys,
    rows=24,
    resized_rows=None,
    keys_before_resize=(),
):
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
    resized_menu = b""

    for key in keys_before_resize:
        os.write(master, key)
        menu += read_until_idle(master, 1)

    if resized_rows is not None:
        fcntl.ioctl(
            master,
            termios.TIOCSWINSZ,
            struct.pack("HHHH", resized_rows, 120, 0, 0),
        )
        resized_menu = read_until_idle(master, 2)

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

    return menu, resized_menu, tail


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

        wide = os.path.join(directory, "wide")
        os.mkdir(wide)
        long_candidate_prefix = "Blackmagic Design-DaVinci Resolve-"
        for suffix in ("Fusion", "Render"):
            open(os.path.join(wide, long_candidate_prefix + suffix), "w").close()

        typed = "printf '<%s>\\n' alpha"
        tall_typed = "printf '<%s>\\n' menu"
        wide_typed = "printf '<%s>\\n' Blackmagic"

        opened, _, _ = run_menu(directory, "tree", typed, [])
        menu_lists_every_candidate = (
            b"alpha-one" in opened
            and b"alpha-two" in opened
            and b"alpha-three" in opened
        )
        menu_opens_with_first_selection = SELECTED_SGR in opened
        selected_start = opened.find(SELECTED_SGR)
        selected_end = opened.find(HIGHLIGHT_RESET, selected_start)
        selected_text = opened[
            selected_start + len(SELECTED_SGR):selected_end
        ]
        selected_highlight_ends_after_entry = (
            selected_start >= 0
            and selected_end >= 0
            and len(selected_text) < 120
        )

        moved, _, accepted = run_menu(
            directory, "tree", typed, [b"\x1b[B", b"\n"]
        )
        a_movement_key_highlights_a_row = SELECTED_SGR in moved
        enter_accepts_the_highlighted_row = (
            b"<alpha-one>" in accepted
            or b"<alpha-two>" in accepted
            or b"<alpha-three>" in accepted
        )

        _, _, tab_accepted = run_menu(directory, "tree", typed, [b"\t"])
        tab_accepts_current_selection = b"<alpha-one>" in tab_accepted

        shifted, _, shifted_accepted = run_menu(
            directory, "tree", typed, [b"\x1b[Z", b"\n"]
        )
        shift_tab_wraps_backward = (
            SELECTED_SGR in shifted and b"<alpha-two>" in shifted_accepted
        )

        _, _, dismissed = run_menu(directory, "tree", typed, [b"\x1b"])
        escape_leaves_the_line_alone = b"<alpha->" in dismissed

        _, _, typed_through = run_menu(directory, "tree", typed, [b"x"])
        an_ordinary_key_reaches_the_line = b"<alpha-x>" in typed_through

        # Eight candidates in an eight row terminal cannot all be shown, so the
        # menu bounds its rows and names the part it drew.
        bounded, _, _ = run_menu(directory, "tall", tall_typed, [], rows=8)
        a_long_list_is_bounded = b"showing 1-" in bounded and b" of 8" in bounded
        the_first_candidate_is_visible = b"menu-1" in bounded

        wide_menu, _, _ = run_menu(directory, "wide", wide_typed, [])
        long_candidate_uses_available_width = (
            long_candidate_prefix.encode() + b"Fusion" in wide_menu
        )

        _, expanded, _ = run_menu(
            directory, "tall", tall_typed, [], rows=8, resized_rows=14
        )
        resize_expands_the_visible_window = b"menu-8" in expanded

        _, contracted, _ = run_menu(
            directory, "tall", tall_typed, [], rows=14, resized_rows=8
        )
        resize_contracts_the_visible_window = (
            b"menu-8" not in contracted
            and b"showing 1-" in contracted
            and b" of 8" in contracted
        )

        _, selected_after_resize, selected_tail = run_menu(
            directory,
            "tall",
            tall_typed,
            [b"\n"],
            rows=14,
            resized_rows=8,
            keys_before_resize=(b"\x1b[B",) * 7,
        )
        resize_keeps_the_selection_visible = (
            SELECTED_SGR in selected_after_resize
            and b"menu-8" in selected_after_resize
        )
        resized_selection_is_accepted = b"<menu-8>" in selected_tail

        prompt_stays_usable = b"MARKER-END" in accepted

        results = {
            "MENU_LISTS_EVERY_CANDIDATE": menu_lists_every_candidate,
            "MENU_OPENS_WITH_FIRST_SELECTION": menu_opens_with_first_selection,
            "SELECTED_HIGHLIGHT_ENDS_AFTER_ENTRY": (
                selected_highlight_ends_after_entry
            ),
            "A_MOVEMENT_KEY_HIGHLIGHTS_A_ROW": a_movement_key_highlights_a_row,
            "ENTER_ACCEPTS_THE_HIGHLIGHTED_ROW": (
                enter_accepts_the_highlighted_row
            ),
            "TAB_ACCEPTS_CURRENT_SELECTION": tab_accepts_current_selection,
            "SHIFT_TAB_WRAPS_BACKWARD": shift_tab_wraps_backward,
            "ESCAPE_LEAVES_THE_LINE_ALONE": escape_leaves_the_line_alone,
            "AN_ORDINARY_KEY_REACHES_THE_LINE": (
                an_ordinary_key_reaches_the_line
            ),
            "A_LONG_LIST_IS_BOUNDED": a_long_list_is_bounded,
            "THE_FIRST_CANDIDATE_IS_VISIBLE": the_first_candidate_is_visible,
            "LONG_CANDIDATE_USES_AVAILABLE_WIDTH": (
                long_candidate_uses_available_width
            ),
            "RESIZE_EXPANDS_THE_VISIBLE_WINDOW": (
                resize_expands_the_visible_window
            ),
            "RESIZE_CONTRACTS_THE_VISIBLE_WINDOW": (
                resize_contracts_the_visible_window
            ),
            "RESIZE_KEEPS_THE_SELECTION_VISIBLE": (
                resize_keeps_the_selection_visible
            ),
            "RESIZED_SELECTION_IS_ACCEPTED": resized_selection_is_accepted,
            "PROMPT_STAYS_USABLE": prompt_stays_usable,
        }

    passed = all(results.values())
    for name, value in results.items():
        print(f"{name}: {value}")
    print("COMPLETION_MENU:", passed)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
