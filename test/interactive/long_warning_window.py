#!/usr/bin/env python3
# A pty harness for the long-warning windowing in src/Errors.cpp. When stderr is
# a terminal and the source line is wider than the terminal, the rendered line is
# trimmed to a window around the caret with ellipses, so the line does not wrap
# and the caret stays under the offending token. A golden cannot capture this,
# since the harness runs non-tty and takes the untrimmed path.
#
# Usage: python3 long_warning_window.py [path-to-shit-binary]
# The binary defaults to ../../shit-dbg relative to this file.
import fcntl, os, re, select, struct, sys, termios, time

here = os.path.dirname(os.path.abspath(__file__))
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "shit-dbg")

COLUMN_COUNT = 80
left = "a" * 120
right = "b" * 120
source_path = "/tmp/long_warning_window.shit"
with open(source_path, "w") as f:
    f.write("echo %s $((1+)) %s\n" % (left, right))

master, slave = os.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, COLUMN_COUNT, 0, 0))

pid = os.fork()
if pid == 0:
    os.setsid()
    fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
    os.dup2(slave, 0)
    os.dup2(slave, 1)
    os.dup2(slave, 2)
    os.close(master)
    os.close(slave)
    os.execv(BIN, [BIN, source_path])
    os._exit(127)

os.close(slave)
buf = b""
deadline = time.time() + 10
while time.time() < deadline:
    r, _, _ = select.select([master], [], [], 0.5)
    if master in r:
        try:
            chunk = os.read(master, 4096)
        except OSError:
            break
        if not chunk:
            break
        buf += chunk
try:
    os.close(master)
except OSError:
    pass
os.waitpid(pid, 0)

text = re.sub(r"\x1b\[[0-9;]*m", "", buf.decode(errors="replace")).replace("\r", "")
lines = text.split("\n")

content_line = next((l for l in lines if "$((1+))" in l), "")
caret_line = next((l for l in lines if "^" in l), "")

within_width = content_line != "" and len(content_line) <= COLUMN_COUNT
has_both_ellipses = content_line.startswith("     1 |  ...") and content_line.endswith("...")
caret_aligned = (
    "$" in content_line
    and "^" in caret_line
    and content_line.index("$") == caret_line.index("^")
)

print("WITHIN_WIDTH:", within_width, "(%d cells)" % len(content_line))
print("BOTH_ELLIPSES:", has_both_ellipses)
print("CARET_ALIGNED:", caret_aligned)
ok = within_width and has_both_ellipses and caret_aligned
print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
