#!/usr/bin/env python3
import fcntl
import os
import pty
import re
import select
import struct
import sys
import termios
import time

here = os.path.dirname(os.path.abspath(__file__))
binary = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "shit-dbg")
column_count = 80
source = "echo %s $((1+)) %s" % ("a" * 120, "b" * 120)

pid, master = pty.fork()
if pid == 0:
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, column_count, 0, 0))
    os.environ["TERM"] = "xterm-256color"
    os.environ.pop("NO_COLOR", None)
    os.execv(binary, [binary, "-c", source])

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
os.waitpid(pid, 0)

raw_lines = output.replace(b"\r", b"").split(b"\n")
raw_content = next((line for line in raw_lines if line.startswith(b"     1 |  ")), b"")
source_highlighted = b"\x1b[" in raw_content
text = re.sub(r"\x1b\[[0-9;:]*m", "", output.decode(errors="replace")).replace("\r", "")
lines = text.split("\n")
content = next((line for line in lines if "$((1+))" in line), "")
caret = next((line for line in lines if "^" in line), "")
within_width = bool(content) and len(content) <= column_count
has_both_ellipses = content.startswith("     1 |  ...") and content.endswith("...")
caret_aligned = "$" in content and "^" in caret and content.index("$") == caret.index("^")
passed = source_highlighted and within_width and has_both_ellipses and caret_aligned
print("SOURCE_HIGHLIGHTED:", source_highlighted)
print("WITHIN_WIDTH:", within_width)
print("BOTH_ELLIPSES:", has_both_ellipses)
print("CARET_ALIGNED:", caret_aligned)
print("RESULT:", "PASS" if passed else "FAIL")
sys.exit(0 if passed else 1)
