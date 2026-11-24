"""prompt.py PROGRAM...: types three lines into PROGRAM on a terminal and checks
that the interpreter runs each one, as a person at the prompt would: each line is
typed once the prompt is shown. (A line typed before it was shown was echoed by
the terminal ahead of the interpreter's banner, and the line's output then
followed the prompt on one line: seen once with the node build.)"""
import os, pty, re, select, sys, time

pid, fd = pty.fork()
if pid == 0:
    os.execvp(sys.argv[1], sys.argv[1:])

# The prompt: a directory and `>', perhaps followed by a line editor's escapes.
PROMPT = re.compile(rb">(?:\x1b\[[0-9;?]*[A-Za-z])*[ \t]*$")


def read(seconds, until_prompt):
    data, end = b"", time.time() + seconds
    while time.time() < end:
        if select.select([fd], [], [], 0.1)[0]:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            data += chunk
        elif until_prompt and b"\n" in data and PROMPT.search(data):
            break  # a new line, then the prompt, and nothing after it for 0.1 s
    return data


seen = read(60, True)
for line in (b"echo first\r", b"echo second\r"):
    os.write(fd, line)
    seen += read(30, True)
os.write(fd, b"exit\r")
seen += read(5, False)
text = seen.decode(errors="replace").replace("\r", "")
lines = [l.strip() for l in text.split("\n")]
ok = "first" in lines and "second" in lines
print(text)
print("prompt: the interpreter ran each line" if ok else "::error::prompt: a typed line did not run")
sys.exit(0 if ok else 1)
