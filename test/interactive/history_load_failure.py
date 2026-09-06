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
damaged_path = os.path.join(directory, "damaged")
with open(damaged_path, "wb") as damaged:
    damaged.write(b"good\n\x01bad\n")

pid, master = pty.fork()
if pid == 0:
    os.environ["KOSH_HISTORY_FILE"] = damaged_path
    os.environ["KOSH_WELCOME"] = ""
    os.execv(binary, [binary, "-Q", "-i"])

os.write(master, b"exit\n")
output = b""
deadline = time.monotonic() + 15
while time.monotonic() < deadline:
    readable, _, _ = select.select([master], [], [], 0.5)
    if master in readable:
        try:
            chunk = os.read(master, 4096)
        except OSError:
            break
        if not chunk:
            break
        output += chunk
    waited, _ = os.waitpid(pid, os.WNOHANG)
    if waited == pid:
        pid = 0
        break

os.close(master)
if pid != 0:
    os.kill(pid, 9)
    os.waitpid(pid, 0)
os.unlink(damaged_path)
os.rmdir(directory)

text = output.decode(errors="replace")
named_the_file = "Unable to read the history at" in text
named_the_reason = "the file contains invalid data" in text
passed = named_the_file and named_the_reason
print("NAMED_THE_FAILURE:", named_the_file)
print("NAMED_THE_REASON:", named_the_reason)
print("RESULT:", "PASS" if passed else "FAIL")
sys.exit(0 if passed else 1)
