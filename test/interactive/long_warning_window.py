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
wide_character = "\u754c"
source = 'test "%s" = $UNSET %s\ntest $OTHER = value' % (
    "a" * 119 + wide_character,
    "b" * 120,
)

pid, master = pty.fork()
if pid == 0:
    fcntl.ioctl(1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, column_count, 0, 0))
    os.environ["TERM"] = "xterm-256color"
    os.environ.pop("NO_COLOR", None)
    os.environ["SHIT_TEST_DIAGNOSTIC_HIGHLIGHT_STATS"] = "1"
    os.execv(binary, [binary, "-n", "-c", source])

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
clipped_string_is_green = b"\x1b[92m" in raw_content
has_no_ansi_underline = b"4:3" not in raw_content
text = re.sub(r"\x1b\[[0-9;:]*m", "", output.decode(errors="replace")).replace("\r", "")
lines = text.split("\n")
content = next((line for line in lines if "$UNSET" in line), "")
caret = next((line for line in lines if "^" in line), "")
highlight_byte_match = re.search(r"diagnostic-highlight-bytes=(\d+)", text)
highlight_bytes = int(highlight_byte_match.group(1)) if highlight_byte_match else 0
highlight_scan_is_bounded = 0 < highlight_bytes <= len(source.encode()) + 1024
content_width = len(content) + content.count(wide_character)
within_width = bool(content) and content_width <= column_count
has_both_ellipses = content.startswith("     1 |  ...") and content.endswith("...")
caret_aligned = (
    "$UNSET" in content
    and "^" in caret
    and content.index("$")
    + content[: content.index("$")].count(wide_character)
    == caret.index("^")
)
passed = (
    source_highlighted
    and clipped_string_is_green
    and has_no_ansi_underline
    and within_width
    and has_both_ellipses
    and caret_aligned
    and highlight_scan_is_bounded
)
print("SOURCE_HIGHLIGHTED:", source_highlighted)
print("CLIPPED_STRING_GREEN:", clipped_string_is_green)
print("NO_ANSI_UNDERLINE:", has_no_ansi_underline)
print("WITHIN_WIDTH:", within_width)
print("BOTH_ELLIPSES:", has_both_ellipses)
print("CARET_ALIGNED:", caret_aligned)
print("HIGHLIGHT_SCAN_BOUNDED:", highlight_scan_is_bounded)
print("RESULT:", "PASS" if passed else "FAIL")
sys.exit(0 if passed else 1)
