#!/usr/bin/env python3
import os
import pty
import select
import sys
import tempfile
import time

here = os.path.dirname(os.path.abspath(__file__))
binary = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "kosh-dbg")

directory = tempfile.mkdtemp()
history_path = os.path.join(directory, "history")

pid, master = pty.fork()
if pid == 0:
    os.environ["KOSH_HISTORY_FILE"] = history_path
    os.environ["KOSH_WELCOME"] = ""
    os.execv(binary, [binary, "-Q", "-i"])

# Every marker is split across the printf format and its operand, so the line
# the terminal echoes back never contains the whole marker.
session = (
    b"kill -TERM $$\n"
    b"printf 'MARK_%s\\n' BLOCKED_SURVIVED\n"
    b"trap 'printf \"FIRE_%s\\n\" TERM_ACTION' TERM\n"
    b"printf 'MARK_%s\\n' INSTALLED\n"
    b"trap - TERM\n"
    b"kill -TERM $$\n"
    b"printf 'MARK_%s\\n' CLEARED_SURVIVED\n"
    b"exit 0\n"
)

for line in session.splitlines(keepends=True):
    os.write(master, line)
    time.sleep(0.05)

output = b""
status = None
is_stream_open = True
deadline = time.monotonic() + 20
while time.monotonic() < deadline:
    if is_stream_open:
        readable, _, _ = select.select([master], [], [], 0.5)
        if master in readable:
            try:
                chunk = os.read(master, 4096)
            except OSError:
                is_stream_open = False
                chunk = b""
            if not chunk:
                is_stream_open = False
            output += chunk
    else:
        time.sleep(0.05)
    waited, waited_status = os.waitpid(pid, os.WNOHANG)
    if waited == pid:
        status = waited_status
        pid = 0
        break

os.close(master)
if pid != 0:
    os.kill(pid, 9)
    os.waitpid(pid, 0)
for leftover in os.listdir(directory):
    os.unlink(os.path.join(directory, leftover))
os.rmdir(directory)

text = output.decode(errors="replace")
term_is_blocked_at_startup = "MARK_BLOCKED_SURVIVED" in text
install_delivers_the_pending_signal = "FIRE_TERM_ACTION" in text
clear_restores_the_block = "MARK_CLEARED_SURVIVED" in text
left_without_a_signal = status is not None and os.waitstatus_to_exitcode(status) == 0
passed = (
    term_is_blocked_at_startup
    and install_delivers_the_pending_signal
    and clear_restores_the_block
    and left_without_a_signal
)

print("TERM_IS_BLOCKED_AT_STARTUP:", term_is_blocked_at_startup)
print("INSTALL_DELIVERS_THE_PENDING_SIGNAL:", install_delivers_the_pending_signal)
print("CLEAR_RESTORES_THE_BLOCK:", clear_restores_the_block)
print("LEFT_WITHOUT_A_SIGNAL:", left_without_a_signal)
print("RESULT:", "PASS" if passed else "FAIL")
sys.exit(0 if passed else 1)
