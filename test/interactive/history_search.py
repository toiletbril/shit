#!/usr/bin/env python3
"""Ctrl-R opens the incremental history search under the plain selector.

The search draws the match, the query, and the hint in the rows below the
prompt, so every check reads the raw transcript of a pty session. No
--tab-selector is passed, since the incremental search is what a session gets
without it. The keys it answers are the keys the interactive menu answers.
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

# The word each command prints is an operand. A drawn match never carries the
# angle bracketed output that proves the entry ran.
SEEDED_COMMANDS = (
    "printf '<%s>\\n' one",
    "printf '<%s>\\n' two",
    "printf '<%s>\\n' three",
)

# A Tab that reached completion would take this name over the last word of the
# accepted match.
COMPLETION_BAIT = "threexyz"

# Every run owns a history file of its own. A shared one would carry the
# entries of the preceding run into the next search.
run_count = 0


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


def run_history_search(directory, keys, rows=24):
    """Seed the history, press ctrl-R, send the keys, then submit the line.

    The transcript is split at ctrl-R. A check can tell what the search drew
    from what the accepted line printed.
    """
    global run_count
    run_count += 1
    history_file = os.path.join(directory, f"history-{run_count}")

    pid, master = pty.fork()
    if pid == 0:
        fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", rows, 120, 0, 0))
        os.environ["TERM"] = "xterm-256color"
        os.environ["HOME"] = directory
        os.environ["KOSH_HISTORY_FILE"] = history_file
        os.chdir(directory)
        os.execv(
            binary,
            [binary, "--norc", "--no-diagnostics", "--tab-selector", "plain"],
        )

    read_until_idle(master, 3)
    for command in SEEDED_COMMANDS:
        os.write(master, command.encode() + b"\n")
        read_until_idle(master, 2)

    os.write(master, b"\x12")
    search = read_until_idle(master, 2)

    for key in keys:
        os.write(master, key)
        search += read_until_idle(master, 1)

    # Accepting a match only rewrites the line. The run needs a submit of its
    # own.
    os.write(master, b"\n")
    search += read_until_idle(master, 2)
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

    return search, tail


def main():
    with tempfile.TemporaryDirectory() as directory:
        with open(os.path.join(directory, COMPLETION_BAIT), "w") as bait:
            bait.write("")

        # The query reaches every seeded command, and the newest of them is
        # drawn first. Enter takes it without running it.
        accepted, marker = run_history_search(directory, [b"printf", b"\n"])
        enter_accepts_the_newest_match = b"<three>" in accepted

        # The hint names each key beside what it does.
        hint_names_the_keys = (
            b"to move" in accepted
            and b"to accept" in accepted
            and b"to cancel" in accepted
        )

        # The down arrow steps back to an older match, the direction ctrl-R
        # walks.
        moved, _ = run_history_search(
            directory, [b"printf", b"\x1b[B", b"\n"]
        )
        the_down_arrow_reaches_an_older_match = b"<two>" in moved

        # The up arrow steps forward again, the direction ctrl-F walks.
        returned, _ = run_history_search(
            directory, [b"printf", b"\x1b[B", b"\x1b[A", b"\n"]
        )
        the_up_arrow_returns_to_a_newer_match = b"<three>" in returned

        # Ctrl-R and ctrl-F keep the movement they always had.
        stepped, _ = run_history_search(
            directory, [b"printf", b"\x12", b"\x12", b"\x06", b"\n"]
        )
        the_control_keys_still_step = b"<two>" in stepped

        # Tab takes the match and is not passed on. A Tab that reached
        # completion would take the bait name over the last word.
        tabbed, _ = run_history_search(directory, [b"printf", b"\t"])
        tab_accepts_the_match = b"<three>" in tabbed
        tab_does_not_reach_completion = b"<" + COMPLETION_BAIT.encode() + b">" \
            not in tabbed

        # Escape puts back the line the search opened on. That line is empty
        # here. The command typed afterwards runs on its own.
        cancelled, _ = run_history_search(
            directory, [b"printf", b"\x1b", b"printf '<%s>\\n' kept", b"\n"]
        )
        escape_leaves_the_line_alone = b"<kept>" in cancelled

        # Ctrl-G cancels the same way.
        aborted, _ = run_history_search(
            directory, [b"printf", b"\x07", b"printf '<%s>\\n' kept", b"\n"]
        )
        control_g_leaves_the_line_alone = b"<kept>" in aborted

        # A query no entry matches leaves the line empty, and the search draws
        # nothing to accept.
        unmatched, _ = run_history_search(directory, [b"zzz", b"\n"])
        an_unmatched_query_accepts_nothing = b"<three>" not in unmatched

        prompt_stays_usable = b"MARKER-END" in marker

        results = {
            "ENTER_ACCEPTS_THE_NEWEST_MATCH": enter_accepts_the_newest_match,
            "HINT_NAMES_THE_KEYS": hint_names_the_keys,
            "THE_DOWN_ARROW_REACHES_AN_OLDER_MATCH": (
                the_down_arrow_reaches_an_older_match
            ),
            "THE_UP_ARROW_RETURNS_TO_A_NEWER_MATCH": (
                the_up_arrow_returns_to_a_newer_match
            ),
            "THE_CONTROL_KEYS_STILL_STEP": the_control_keys_still_step,
            "TAB_ACCEPTS_THE_MATCH": tab_accepts_the_match,
            "TAB_DOES_NOT_REACH_COMPLETION": tab_does_not_reach_completion,
            "ESCAPE_LEAVES_THE_LINE_ALONE": escape_leaves_the_line_alone,
            "CONTROL_G_LEAVES_THE_LINE_ALONE": control_g_leaves_the_line_alone,
            "AN_UNMATCHED_QUERY_ACCEPTS_NOTHING": (
                an_unmatched_query_accepts_nothing
            ),
            "PROMPT_STAYS_USABLE": prompt_stays_usable,
        }

    passed = all(results.values())
    for name, value in results.items():
        print(f"{name}: {value}")
    print("HISTORY_SEARCH:", passed)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
