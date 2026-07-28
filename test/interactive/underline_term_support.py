#!/usr/bin/env python3
import os
import pty
import select
import signal
import sys
import tempfile
import time

binary = sys.argv[1]


def read_until_idle(master, timeout):
    output = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        readable, _, _ = select.select([master], [], [], 0.1)
        if master not in readable:
            if output:
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


def render_unknown_command(term):
    with tempfile.TemporaryDirectory() as directory:
        pid, master = pty.fork()
        if pid == 0:
            os.environ["TERM"] = term
            os.environ["HOME"] = directory
            os.environ["SHIT_HISTORY"] = os.path.join(directory, "history")
            os.environ.pop("NO_COLOR", None)
            os.execv(binary, [binary, "--norc"])

        read_until_idle(master, 3)
        os.write(master, b"underline_missing_command")
        output = read_until_idle(master, 2)
        os.write(master, b"\x03exit\n")
        read_until_idle(master, 1)
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

        return output


generic_output = render_unknown_command("xterm-256color")
kitty_output = render_unknown_command("xterm-kitty")
generic_has_no_underline = b"4:3" not in generic_output
kitty_has_underline = b"4:3" in kitty_output
passed = generic_has_no_underline and kitty_has_underline
print("GENERIC_NO_UNDERLINE:", generic_has_no_underline)
print("KITTY_UNDERLINE:", kitty_has_underline)
print("TERM_UNDERLINE_POLICY:", passed)
sys.exit(0 if passed else 1)
