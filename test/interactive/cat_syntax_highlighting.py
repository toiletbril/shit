#!/usr/bin/env python3
import fcntl
import os
import pty
import select
import struct
import sys
import tempfile
import termios
import time

binary = sys.argv[1]
with tempfile.NamedTemporaryFile(delete=False) as source:
    source.write(b'#!/usr/bin/env bash\r\necho "styled"\r\n')
    source_path = source.name

pid, master = pty.fork()
if pid == 0:
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    os.environ["TERM"] = "xterm-256color"
    os.environ.pop("NO_COLOR", None)
    command = "shitbox cat --syntax-highlighting '%s'" % source_path
    os.execv(binary, [binary, "-c", command])

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
_, status = os.waitpid(pid, 0)
os.unlink(source_path)
passed = os.waitstatus_to_exitcode(status) == 0 and b"\x1b[" in output
print("TERMINAL_HIGHLIGHTING:", passed)
sys.exit(0 if passed else 1)
