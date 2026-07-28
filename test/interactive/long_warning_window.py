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
source_line = 'test "%s" = $UNSET %s; test $OTHER = value' % (
    "a" * 119 + wide_character,
    "b" * 120,
)
single_quote_line = "single_end'; test $SINGLE_UNSET = value"
double_quote_line = 'double_end"; test $DOUBLE_UNSET = value'
nested_substitution_line = ') tail"; test $NESTED_UNSET = value'
source = (
    '\n' * 2048
    + "value='single_start\n"
    + single_quote_line
    + '\nvalue="double_start\n$(printf value)\n'
    + double_quote_line
    + '\nvalue="$(printf value\n'
    + nested_substitution_line
    + '\n'
    + source_line
)

def run_tty(arguments):
    pid, master = pty.fork()
    if pid == 0:
        fcntl.ioctl(
            1, termios.TIOCSWINSZ, struct.pack("HHHH", 24, column_count, 0, 0)
        )
        os.environ["TERM"] = "xterm-256color"
        os.environ.pop("NO_COLOR", None)
        os.environ["SHIT_TEST_DIAGNOSTIC_HIGHLIGHT_STATS"] = "1"
        os.execv(binary, [binary] + arguments)

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
    return output


output = run_tty(["-n", "-c", source])
heredoc_output = run_tty(["-c", "cat <<EOF\n$(bad_command)\nEOF"])

raw_lines = output.replace(b"\r", b"").split(b"\n")
raw_content = next((line for line in raw_lines if b"$UNSET" in line), b"")
raw_single_quote = next(
    (line for line in raw_lines if b"$SINGLE_UNSET" in line), b""
)
raw_double_quote = next(
    (line for line in raw_lines if b"$DOUBLE_UNSET" in line), b""
)
raw_nested_substitution = next(
    (line for line in raw_lines if b"$NESTED_UNSET" in line), b""
)
source_highlighted = b"\x1b[" in raw_content
clipped_string_is_green = b"\x1b[92m" in raw_content
single_quote_context_is_green = b"\x1b[92msingle_end'\x1b[0m" in raw_single_quote
double_quote_context_is_green = b'\x1b[92mdouble_end"\x1b[0m' in raw_double_quote
nested_parent_quote_is_green = (
    b'\x1b[92m tail"\x1b[0m' in raw_nested_substitution
)
heredoc_trace_is_green = b"\x1b[92m$(bad_command)\x1b[0m" in heredoc_output
has_no_ansi_underline = b"4:3" not in raw_content
text = re.sub(r"\x1b\[[0-9;:]*m", "", output.decode(errors="replace")).replace("\r", "")
lines = text.split("\n")
content_index = next(
    (index for index, line in enumerate(lines) if "$UNSET" in line), -1
)
content = lines[content_index] if content_index >= 0 else ""
caret = next((line for line in lines[content_index + 1 :] if "^" in line), "")
highlight_byte_match = re.search(r"diagnostic-highlight-bytes=(\d+)", text)
highlight_bytes = int(highlight_byte_match.group(1)) if highlight_byte_match else 0
diagnostic_line_bytes = sum(
    len(line.encode())
    for line in (
        single_quote_line,
        double_quote_line,
        nested_substitution_line,
        source_line,
    )
)
highlight_scan_is_bounded = 0 < highlight_bytes <= diagnostic_line_bytes + 16
content_width = len(content) + content.count(wide_character)
within_width = bool(content) and content_width <= column_count
has_both_ellipses = "|  ..." in content and content.endswith("...")
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
    and single_quote_context_is_green
    and double_quote_context_is_green
    and nested_parent_quote_is_green
    and heredoc_trace_is_green
    and has_no_ansi_underline
    and within_width
    and has_both_ellipses
    and caret_aligned
    and highlight_scan_is_bounded
)
print("SOURCE_HIGHLIGHTED:", source_highlighted)
print("CLIPPED_STRING_GREEN:", clipped_string_is_green)
print("SINGLE_QUOTE_CONTEXT_GREEN:", single_quote_context_is_green)
print("DOUBLE_QUOTE_CONTEXT_GREEN:", double_quote_context_is_green)
print("NESTED_PARENT_QUOTE_GREEN:", nested_parent_quote_is_green)
print("HEREDOC_TRACE_GREEN:", heredoc_trace_is_green)
print("NO_ANSI_UNDERLINE:", has_no_ansi_underline)
print("WITHIN_WIDTH:", within_width)
print("BOTH_ELLIPSES:", has_both_ellipses)
print("CARET_ALIGNED:", caret_aligned)
print("HIGHLIGHT_SCAN_BOUNDED:", highlight_scan_is_bounded)
print("RESULT:", "PASS" if passed else "FAIL")
sys.exit(0 if passed else 1)
