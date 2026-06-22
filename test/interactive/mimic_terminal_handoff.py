#!/usr/bin/env python3
# A manual pty harness for the mimic-job terminal handoff, driven by hand rather
# than wired into make test, since the defect it guards is timing-based and a
# golden cannot capture it. It launches an interactive shit session whose
# controlling terminal is a pty with TOSTOP enabled, runs a mimicked foreground
# command, and checks that the job runs to completion and the prompt returns.
#
# Before the fork synchronization in src/Utils.cpp, the forked child could touch
# the terminal before the parent handed it over with give_controlling_terminal_to
# and stop on SIGTTOU, hanging the session. The sync pipe blocks the child until
# the handoff, so the job runs and the prompt comes back. A deadlock in the sync
# wiring would hang here and fail on the timeout.
#
# Usage: python3 mimic_terminal_handoff.py [path-to-shit-binary]
# The binary defaults to ../../shit-dbg relative to this file.
import os, pty, select, sys, time

here = os.path.dirname(os.path.abspath(__file__))
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "shit-dbg")

script = "/tmp/handoff_mim.bash"
with open(script, "w") as f:
    f.write("#!/bin/bash\necho MIMIC_RAN_OK\n")
os.chmod(script, 0o755)

pid, master = pty.fork()
if pid == 0:
    os.execv(BIN, [BIN, "-I", "-i", "--mood", "bash"])
    os._exit(127)

def feed(text):
    os.write(master, text.encode())
    time.sleep(0.3)

buf = b""
deadline = time.time() + 15
def pump(until_marker):
    global buf
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
            if until_marker.encode() in buf:
                return True
    return False

# Enable TOSTOP so a background-pgrp write would stop on SIGTTOU.
feed("stty tostop\n")
# Run the mimicked foreground script, then a marker that only prints once the
# prompt returns after the job completes.
feed(f"{script}\n")
feed("echo HANDOFF_PROMPT_BACK\n")
pump("HANDOFF_PROMPT_BACK")
feed("exit\n")
try:
    os.close(master)
except OSError:
    pass
os.waitpid(pid, 0)

text = buf.decode(errors="replace")
ran = "MIMIC_RAN_OK" in text
back = "HANDOFF_PROMPT_BACK" in text
print("MIMIC_RAN_OK:", ran)
print("PROMPT_BACK:", back)
print("RESULT:", "PASS" if (ran and back) else "FAIL")
sys.exit(0 if (ran and back) else 1)
