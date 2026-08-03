#!/usr/bin/env python3
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

binary = sys.argv[1]


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


def render_highlighted_line(term):
    with tempfile.TemporaryDirectory() as directory:
        pid, master = pty.fork()
        if pid == 0:
            fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 240, 0, 0))
            os.environ["TERM"] = term
            os.environ["HOME"] = directory
            os.environ["SHIT_HISTORY"] = os.path.join(directory, "history")
            os.environ.pop("NO_COLOR", None)
            os.environ.pop("PALETTE_UNSET", None)
            os.execv(binary, [binary, "--norc"])

        read_until_idle(master, 3)
        os.write(
            master,
            b'if true && false; then echo "$PATH" "$PALETTE_UNSET" >out; fi; '
            b"underline_missing_command",
        )
        output = read_until_idle(master, 2, b"underline_missing_command")
        os.write(master, b"\x03exit\n")
        read_until_idle(master, 1)
        os.close(master)

        deadline = time.monotonic() + 2
        child_exited_cleanly = False
        while time.monotonic() < deadline:
            waited, status = os.waitpid(pid, os.WNOHANG)
            if waited == pid:
                child_exited_cleanly = os.waitstatus_to_exitcode(status) == 0
                break
            time.sleep(0.02)
        else:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)

        return output, child_exited_cleanly


generic_output, generic_child_exited_cleanly = render_highlighted_line(
    "xterm-256color"
)
kitty_output, kitty_child_exited_cleanly = render_highlighted_line("xterm-kitty")
generic_has_no_underline = b"4:3" not in generic_output
kitty_has_underline = b"4:3" in kitty_output
shared_palette_tokens = (
    b"\x1b[1;35mif\x1b[0m",
    b"\x1b[1;35m&&\x1b[0m",
    b"\x1b[1;35m>\x1b[0m",
    b"\x1b[96m$PATH\x1b[0m",
)
shared_palette_is_applied = all(
    all(token in output for token in shared_palette_tokens)
    for output in (generic_output, kitty_output)
)
generic_unset_is_red = b"\x1b[91m$PALETTE_UNSET\x1b[0m" in generic_output
kitty_unset_is_underlined = (
    b"\x1b[91;4:3;58:5:10m$PALETTE_UNSET\x1b[0m" in kitty_output
)
generic_unknown_is_red = (
    b"\x1b[91munderline_missing_command\x1b[0m" in generic_output
)
kitty_unknown_is_underlined = (
    b"\x1b[91;4:3;58:5:10munderline_missing_command\x1b[0m" in kitty_output
)
passed = (
    generic_child_exited_cleanly
    and kitty_child_exited_cleanly
    and generic_has_no_underline
    and kitty_has_underline
    and shared_palette_is_applied
    and generic_unset_is_red
    and kitty_unset_is_underlined
    and generic_unknown_is_red
    and kitty_unknown_is_underlined
)
print("GENERIC_CHILD_EXITED_CLEANLY:", generic_child_exited_cleanly)
print("KITTY_CHILD_EXITED_CLEANLY:", kitty_child_exited_cleanly)
print("GENERIC_NO_UNDERLINE:", generic_has_no_underline)
print("KITTY_UNDERLINE:", kitty_has_underline)
print("SHARED_PALETTE_IS_APPLIED:", shared_palette_is_applied)
print("GENERIC_UNSET_IS_RED:", generic_unset_is_red)
print("KITTY_UNSET_IS_UNDERLINED:", kitty_unset_is_underlined)
print("GENERIC_UNKNOWN_IS_RED:", generic_unknown_is_red)
print("KITTY_UNKNOWN_IS_UNDERLINED:", kitty_unknown_is_underlined)
print("TERM_UNDERLINE_POLICY:", passed)
sys.exit(0 if passed else 1)
