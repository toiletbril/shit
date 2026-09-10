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
source_contents = (
    b'#!/usr/bin/env bash\r\n'
    b'RESOLVED_VARIABLE=$PATH\r\n'
    b'QUOTED_ASSIGNMENT="$PATH"\r\n'
    b'for LOOP_VARIABLE in one; do echo "$LOOP_VARIABLE"; done\r\n'
    b'echo "$PATH" ${PATH} $((PATH + 1))\r\n'
    b'echo "$CAT_UNSET_VARIABLE" ${CAT_BRACED_UNSET} '
    b'$((CAT_ARITHMETIC_UNSET + 1))\r\n'
    b'if true && false || true; then : >out; fi\r\n'
    b'echo "styled" $"localized $PATH"\r\n'
    b'finish() { :; }\r\n'
    b'finish\r\n'
    b'missing_command\r\n'
    b'cat <<_ACEOF ignored_argument\r\n'
    b'heredoc body\r\n'
    b'_ACEOF\r\n'
    b'ech'
)
with tempfile.NamedTemporaryFile(delete=False) as source:
    source.write(source_contents)
    source_path = source.name

pid, master = pty.fork()
if pid == 0:
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    attributes = termios.tcgetattr(1)
    attributes[1] &= ~termios.ONLCR
    termios.tcsetattr(1, termios.TCSANOW, attributes)
    os.environ["TERM"] = "xterm-256color"
    os.environ.pop("NO_COLOR", None)
    os.environ.pop("CAT_UNSET_VARIABLE", None)
    os.environ.pop("CAT_BRACED_UNSET", None)
    os.environ.pop("CAT_ARITHMETIC_UNSET", None)
    command = "koshkit cat --syntax-highlighting '%s'" % source_path
    os.execv(
        binary, [binary, "--norc", "--no-diagnostics", "-c", command]
    )

output = b""
deadline = time.monotonic() + 10
while time.monotonic() < deadline:
    readable, _, _ = select.select([master], [], [], 0.5)
    if master not in readable:
        continue
    try:
        chunk = os.read(master, 4096)
    except OSError:
        break
    if not chunk:
        break
    output += chunk

os.close(master)
child_exited_cleanly = False
reap_deadline = time.monotonic() + 2
while time.monotonic() < reap_deadline:
    waited, status = os.waitpid(pid, os.WNOHANG)
    if waited == pid:
        child_exited_cleanly = os.waitstatus_to_exitcode(status) == 0
        break
    time.sleep(0.02)
else:
    os.kill(pid, signal.SIGKILL)
    os.waitpid(pid, 0)
os.unlink(source_path)
plain_output = bytearray()
output_position = 0
while output_position < len(output):
    if output[output_position] != 0x1B:
        plain_output.append(output[output_position])
        output_position += 1
        continue

    output_position += 1
    if output_position >= len(output):
        break
    if output[output_position] == ord("["):
        output_position += 1
        while output_position < len(output) and not (
            0x40 <= output[output_position] <= 0x7E
        ):
            output_position += 1
        if output_position < len(output):
            output_position += 1
    else:
        output_position += 1

plain_output_bytes = bytes(plain_output)
has_exact_source_bytes = plain_output_bytes == source_contents
command_forms_are_resolved = {
    "function": b"\x1b[34mfinish\x1b[0m" in output,
    "unknown": b"\x1b[34mmissing_command\x1b[0m" in output,
    "partial": b"\x1b[34mech\x1b[0m" in output,
}
variable_forms_are_resolved = {
    "assignment": b"\x1b[96mRESOLVED_VARIABLE\x1b[0m" in output,
    "quoted-assignment": b"\x1b[96mQUOTED_ASSIGNMENT\x1b[0m" in output,
    "loop": b"\x1b[96mLOOP_VARIABLE\x1b[0m" in output,
    "dollar": b"\x1b[96m$PATH\x1b[0m" in output,
    "braced": b"\x1b[96m${PATH}\x1b[0m" in output,
    "arithmetic": b"\x1b[96mPATH\x1b[0m" in output,
    "unset-dollar": b"\x1b[96m$CAT_UNSET_VARIABLE\x1b[0m" in output,
    "unset-braced": b"\x1b[96m${CAT_BRACED_UNSET}\x1b[0m" in output,
    "unset-arithmetic": b"\x1b[96mCAT_ARITHMETIC_UNSET\x1b[0m" in output,
}
syntax_forms_use_requested_palette = {
    "keyword": b"\x1b[1;35mif\x1b[0m" in output,
    "and": b"\x1b[1;35m&&\x1b[0m" in output,
    "or": b"\x1b[1;35m||\x1b[0m" in output,
    "redirection": b"\x1b[1;35m>\x1b[0m" in output,
}
heredoc_forms_are_separated = {
    "opener-and-closer": output.count(b"\x1b[1;92m_ACEOF\x1b[0m") == 2,
    "opener-argument-is-plain": b"\x1b[1;92m_ACEOF\x1b[0m ignored_argument"
    in output,
    "body": b"\x1b[92mheredoc body" in output,
}
has_no_underline = b"4:3" not in output
passed = (
    child_exited_cleanly
    and b"\x1b[" in output
    and all(command_forms_are_resolved.values())
    and all(variable_forms_are_resolved.values())
    and all(syntax_forms_use_requested_palette.values())
    and all(heredoc_forms_are_separated.values())
    and has_no_underline
    and has_exact_source_bytes
)
print("CHILD_EXITED_CLEANLY:", child_exited_cleanly)
print("COMMAND_FORMS_RESOLVED:", command_forms_are_resolved)
print("VARIABLE_FORMS_RESOLVED:", variable_forms_are_resolved)
print("SYNTAX_FORMS_USE_REQUESTED_PALETTE:", syntax_forms_use_requested_palette)
print("HEREDOC_FORMS_SEPARATED:", heredoc_forms_are_separated)
print("NO_UNDERLINE:", has_no_underline)
print("EXACT_SOURCE_BYTES:", has_exact_source_bytes)
if not has_exact_source_bytes:
    difference_position = 0
    while (
        difference_position < len(plain_output_bytes)
        and difference_position < len(source_contents)
        and plain_output_bytes[difference_position]
        == source_contents[difference_position]
    ):
        difference_position += 1

    window_start = max(0, difference_position - 24)
    window_end = difference_position + 24
    print("DIFFERENCE_POSITION:", difference_position)
    print("EXPECTED_LENGTH:", len(source_contents))
    print("RECEIVED_LENGTH:", len(plain_output_bytes))
    print("EXPECTED_WINDOW:", source_contents[window_start:window_end])
    print("RECEIVED_WINDOW:", plain_output_bytes[window_start:window_end])
    print("RAW_HEAD:", output[:160])
print("TERMINAL_HIGHLIGHTING:", passed)
sys.exit(0 if passed else 1)
