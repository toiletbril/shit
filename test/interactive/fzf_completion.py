#!/usr/bin/env python3
"""An explicit tab can route its candidates through an interactive selector.

The selector is stubbed rather than run for real, so the suite needs no fzf
installed and stays deterministic. The stub reads the NUL separated records the
shell writes, records them, and echoes back the ones it was told to pick, which
is the same subset contract fzf has.
"""

import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import tempfile
import termios
import time

# Each scenario runs from the candidate tree, so the binary is resolved before
# any directory change.
binary = os.path.abspath(sys.argv[1])

# A diagnostic is colored on a terminal. A reset sequence sits between the
# severity word and the colon that follows it.
ANSI = re.compile(rb"\x1b\[[0-9;?]*[a-zA-Z]|\x1b\][^\x07]*\x07")


def without_ansi(data):
    return ANSI.sub(b"", data)


SELECTOR_SOURCE = """#!/usr/bin/env python3
import os
import sys

records = [record for record in sys.stdin.buffer.read().split(b"\\0") if record]

log_path = os.environ.get("STUB_LOG")
if log_path:
    with open(log_path, "ab") as log:
        log.write(b"ran\\n")
        for record in records:
            log.write(b"record " + record + b"\\n")

if os.environ.get("STUB_CANCEL"):
    sys.exit(130)

if os.environ.get("STUB_FAIL"):
    sys.exit(int(os.environ["STUB_FAIL"]))

picks = os.environ.get("STUB_PICK", "1").split()
for pick in picks:
    sys.stdout.buffer.write(records[int(pick) - 1] + b"\\0")
"""


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


def reap(pid):
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        waited, _ = os.waitpid(pid, os.WNOHANG)
        if waited == pid:
            return
        time.sleep(0.02)
    os.kill(pid, signal.SIGKILL)
    os.waitpid(pid, 0)


def run_variable_probe(directory):
    """Read the selector variables from a session that never set them."""
    pid, master = pty.fork()
    if pid == 0:
        fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 120, 0, 0))
        os.environ["TERM"] = "xterm-256color"
        os.environ["HOME"] = directory
        os.environ["KOSH_HISTORY_FILE"] = os.path.join(directory, "history")
        os.environ.pop("KOSH_FZF_COMPLETION_COMMAND", None)
        os.environ.pop("KOSH_FZF_COMPLETION_OPTS", None)
        os.chdir(os.path.join(directory, "tree"))
        os.execv(binary, [binary, "--norc", "--no-diagnostics"])

    read_until_idle(master, 3)
    os.write(
        master,
        b'printf "seeded (%s) (%s)\\n"'
        b' "$KOSH_FZF_COMPLETION_COMMAND" "$KOSH_FZF_COMPLETION_OPTS"\n',
    )
    output = read_until_idle(master, 3)
    os.write(master, b"exit\n")
    output += read_until_idle(master, 2)
    os.close(master)
    reap(pid)
    return output


def run_interrupt_scenario(directory):
    """Tab into a spec that never finishes, then interrupt it.

    A real one is a cloud tool waiting on an unreachable endpoint. Raw mode
    normally turns the interrupt key into an ordinary byte, so without the
    handoff the key would queue as input and the prompt would stay stuck.
    """
    pid, master = pty.fork()
    if pid == 0:
        fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 120, 0, 0))
        os.environ["TERM"] = "xterm-256color"
        os.environ["HOME"] = directory
        os.environ["KOSH_HISTORY_FILE"] = os.path.join(directory, "history")
        os.chdir(os.path.join(directory, "tree"))
        os.execv(
            binary,
            [binary, "--norc", "--no-diagnostics", "--tab-selector", "plain"],
        )

    read_until_idle(master, 3)
    os.write(master, b"_stall() { sleep 60; COMPREPLY=(late); }\n")
    read_until_idle(master, 1)
    os.write(master, b"complete -F _stall stallcmd\n")
    read_until_idle(master, 1)
    os.write(master, b"stallcmd ")
    read_until_idle(master, 1)

    os.write(master, b"\t")
    read_until_idle(master, 2)

    started = time.monotonic()
    os.write(master, b"\x03")
    output = read_until_idle(master, 8, b"MARKER-BACK")
    os.write(master, b"printf 'MARKER-BACK\\n'\nexit\n")
    output += read_until_idle(master, 3)
    elapsed = time.monotonic() - started
    os.close(master)

    reap(pid)

    return output, elapsed


def run_scenario(
    directory, selector, environment, typed, mode="external", tab_count=1
):
    """Type the words, press tab, submit, and return the transcript and log."""
    log_path = os.path.join(directory, "selector-log")
    # A command left by an earlier scenario would come back as a ghost and put
    # its candidate names on this screen.
    for stale in (log_path, os.path.join(directory, "history")):
        if os.path.exists(stale):
            os.remove(stale)

    pid, master = pty.fork()
    if pid == 0:
        fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 120, 0, 0))
        os.environ["TERM"] = "xterm-256color"
        os.environ["HOME"] = directory
        os.environ["KOSH_HISTORY_FILE"] = os.path.join(directory, "history")
        os.environ["STUB_LOG"] = log_path
        os.environ["KOSH_FZF_COMPLETION_COMMAND"] = selector
        os.environ.pop("STUB_CANCEL", None)
        os.environ.pop("STUB_PICK", None)
        os.environ.pop("STUB_FAIL", None)
        os.environ.update(environment)
        os.chdir(os.path.join(directory, "tree"))
        os.execv(
            binary,
            [binary, "--norc", "--no-diagnostics", "--tab-selector", mode],
        )

    read_until_idle(master, 3)
    os.write(master, typed.encode())
    # What the tab draws is part of the result. Every read is kept.
    output = read_until_idle(master, 1)
    for _ in range(tab_count):
        os.write(master, b"\t")
        output += read_until_idle(master, 3)
    os.write(master, b"\n")
    output += read_until_idle(master, 2, b"MARKER-END")
    os.write(master, b"printf 'MARKER-END\\n'\nexit\n")
    output += read_until_idle(master, 2)
    os.close(master)

    reap(pid)

    log = b""
    if os.path.exists(log_path):
        with open(log_path, "rb") as handle:
            log = handle.read()
    return output, log


def main():
    with tempfile.TemporaryDirectory() as directory:
        tree = os.path.join(directory, "tree")
        os.mkdir(tree)
        for name in ("alpha-one", "alpha-two", "alpha-three", "gamma-solo"):
            open(os.path.join(tree, name), "w").close()
        open(os.path.join(tree, "beta only"), "w").close()
        open(os.path.join(tree, "beta two"), "w").close()
        open(os.path.join(tree, "hash#one"), "w").close()
        open(os.path.join(tree, "hash#two"), "w").close()

        selector = os.path.join(directory, "selector")
        with open(selector, "w") as handle:
            handle.write(SELECTOR_SOURCE)
        os.chmod(selector, 0o755)

        typed = "printf '<%s>\\n' alpha"

        prefixed, prefixed_log = run_scenario(
            directory, selector, {}, typed
        )
        first_tab_extends_the_prefix = (
            prefixed_log == b"" and b"<alpha->" in prefixed
        )

        picked, picked_log = run_scenario(
            directory, selector, {"STUB_PICK": "3"}, typed, tab_count=2
        )
        selector_saw_every_candidate = (
            b"record alpha-one" in picked_log
            and b"record alpha-two" in picked_log
            and b"record alpha-three" in picked_log
        )
        pick_replaced_the_token = b"<alpha-two>" in picked

        multi, _ = run_scenario(
            directory, selector, {"STUB_PICK": "1 2"}, typed, tab_count=2
        )
        several_picks_are_joined = (
            b"<alpha-one>" in multi and b"<alpha-three>" in multi
        )

        cancelled, cancelled_log = run_scenario(
            directory, selector, {"STUB_CANCEL": "1"}, typed, tab_count=2
        )
        cancel_ran_the_selector = b"ran\n" in cancelled_log
        cancel_leaves_the_line_alone = b"<alpha->" in cancelled
        # Falling back to the printed list here would dump every candidate over
        # the screen the user just dismissed.
        cancel_prints_no_list = (
            b"alpha-one" not in cancelled and b"alpha-three" not in cancelled
        )

        plain, plain_log = run_scenario(
            directory, selector, {}, typed, mode="plain"
        )
        plain_skips_the_selector = plain_log == b""
        plain_keeps_the_static_completion = b"<alpha->" in plain

        lone, lone_log = run_scenario(
            directory,
            selector,
            {"STUB_PICK": "1"},
            "printf '<%s>\\n' gamma",
        )
        one_candidate_skips_the_selector = lone_log == b""
        one_candidate_is_inserted = b"<gamma-solo>" in lone

        quoted, quoted_log = run_scenario(
            directory,
            selector,
            {"STUB_PICK": "1"},
            "printf '<%s>\\n' bet",
            tab_count=2,
        )
        quoting_survives_the_selector = (
            b"record beta\\ only" in quoted_log and b"<beta only>" in quoted
        )

        quoted_multi, _ = run_scenario(
            directory,
            selector,
            {"STUB_PICK": "1 2"},
            "printf '<%s>\\n' bet",
            tab_count=2,
        )
        several_quoted_picks_are_joined = (
            b"<beta only>" in quoted_multi and b"<beta two>" in quoted_multi
        )

        metacharacter, metacharacter_log = run_scenario(
            directory,
            selector,
            {"STUB_PICK": "1"},
            "printf '<%s>\\n' hash",
            tab_count=2,
        )
        metacharacter_is_quoted = (
            b"record hash\\#one" in metacharacter_log
            and b"<hash#one>" in metacharacter
        )

        seeded = run_variable_probe(directory)
        the_command_variable_is_seeded = b"seeded (fzf)" in seeded
        the_options_variable_is_seeded = b"--layout=reverse" in seeded

        empty, empty_log = run_scenario(
            directory,
            selector,
            {"KOSH_FZF_COMPLETION_COMMAND": ""},
            typed,
            tab_count=2,
        )
        an_empty_command_is_reported = b"names no tab selector" in empty
        an_empty_command_skips_the_selector = empty_log == b""
        an_empty_command_prints_the_list = b"alpha-three" in empty

        missing, missing_log = run_scenario(
            directory,
            selector,
            {"KOSH_FZF_COMPLETION_COMMAND": "kosh_absent_selector_zzqq"},
            typed,
            tab_count=2,
        )
        a_missing_command_is_reported = b"was not found" in missing
        a_missing_command_skips_the_selector = missing_log == b""
        a_missing_command_prints_the_list = b"alpha-three" in missing

        # The failure carries the severity of a real error and a note naming
        # both variables with the values they hold.
        missing_plain = without_ansi(missing)
        a_missing_command_is_an_error = (
            b"error: The tab selector" in missing_plain
        )
        a_missing_command_names_its_command_variable = (
            b"note: KOSH_FZF_COMPLETION_COMMAND is "
            b"'kosh_absent_selector_zzqq'" in missing_plain
        )
        a_missing_command_names_its_options_variable = (
            b"KOSH_FZF_COMPLETION_OPTS is " in missing_plain
        )

        failed, failed_log = run_scenario(
            directory, selector, {"STUB_FAIL": "3"}, typed, tab_count=2
        )
        a_failing_selector_ran = b"ran\n" in failed_log
        a_failing_selector_is_reported = b"exited with status 3" in failed
        a_failing_selector_prints_the_list = b"alpha-three" in failed

        prompt_stays_usable = b"MARKER-END" in picked

        interrupted, interrupt_seconds = run_interrupt_scenario(directory)
        interrupt_frees_the_prompt = b"MARKER-BACK" in interrupted
        interrupt_is_prompt = interrupt_seconds < 20
        interrupt_drops_the_candidates = b"late" not in interrupted

        results = {
            "FIRST_TAB_EXTENDS_THE_PREFIX": first_tab_extends_the_prefix,
            "SELECTOR_SAW_EVERY_CANDIDATE": selector_saw_every_candidate,
            "PICK_REPLACED_THE_TOKEN": pick_replaced_the_token,
            "SEVERAL_PICKS_ARE_JOINED": several_picks_are_joined,
            "CANCEL_RAN_THE_SELECTOR": cancel_ran_the_selector,
            "CANCEL_LEAVES_THE_LINE_ALONE": cancel_leaves_the_line_alone,
            "CANCEL_PRINTS_NO_LIST": cancel_prints_no_list,
            "PLAIN_SKIPS_THE_SELECTOR": plain_skips_the_selector,
            "PLAIN_KEEPS_THE_STATIC_COMPLETION": (
                plain_keeps_the_static_completion
            ),
            "ONE_CANDIDATE_SKIPS_THE_SELECTOR": one_candidate_skips_the_selector,
            "ONE_CANDIDATE_IS_INSERTED": one_candidate_is_inserted,
            "QUOTING_SURVIVES_THE_SELECTOR": quoting_survives_the_selector,
            "SEVERAL_QUOTED_PICKS_ARE_JOINED": several_quoted_picks_are_joined,
            "METACHARACTER_IS_QUOTED": metacharacter_is_quoted,
            "THE_COMMAND_VARIABLE_IS_SEEDED": the_command_variable_is_seeded,
            "THE_OPTIONS_VARIABLE_IS_SEEDED": the_options_variable_is_seeded,
            "AN_EMPTY_COMMAND_IS_REPORTED": an_empty_command_is_reported,
            "AN_EMPTY_COMMAND_SKIPS_THE_SELECTOR": (
                an_empty_command_skips_the_selector
            ),
            "AN_EMPTY_COMMAND_PRINTS_THE_LIST": an_empty_command_prints_the_list,
            "A_MISSING_COMMAND_IS_REPORTED": a_missing_command_is_reported,
            "A_MISSING_COMMAND_SKIPS_THE_SELECTOR": (
                a_missing_command_skips_the_selector
            ),
            "A_MISSING_COMMAND_PRINTS_THE_LIST": (
                a_missing_command_prints_the_list
            ),
            "A_MISSING_COMMAND_IS_AN_ERROR": a_missing_command_is_an_error,
            "A_MISSING_COMMAND_NAMES_ITS_COMMAND_VARIABLE": (
                a_missing_command_names_its_command_variable
            ),
            "A_MISSING_COMMAND_NAMES_ITS_OPTIONS_VARIABLE": (
                a_missing_command_names_its_options_variable
            ),
            "A_FAILING_SELECTOR_RAN": a_failing_selector_ran,
            "A_FAILING_SELECTOR_IS_REPORTED": a_failing_selector_is_reported,
            "A_FAILING_SELECTOR_PRINTS_THE_LIST": (
                a_failing_selector_prints_the_list
            ),
            "PROMPT_STAYS_USABLE": prompt_stays_usable,
            "INTERRUPT_FREES_THE_PROMPT": interrupt_frees_the_prompt,
            "INTERRUPT_IS_PROMPT": interrupt_is_prompt,
            "INTERRUPT_DROPS_THE_CANDIDATES": interrupt_drops_the_candidates,
        }

    passed = all(results.values())
    for name, value in results.items():
        print(f"{name}: {value}")
    print("FZF_COMPLETION:", passed)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
