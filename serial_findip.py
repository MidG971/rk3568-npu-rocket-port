#!/usr/bin/env python3
# Log in over serial (radxa/radxa) and print `ip addr` to find the board's current IP.
import os, time, select, sys

DEV = "/dev/ttyUSB0"
fd = os.open(DEV, os.O_RDWR | os.O_NOCTTY)

def drain(timeout=1.0):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try: buf += os.read(fd, 4096)
            except OSError: break
    return buf.decode("utf-8", "replace")

def send(s):
    os.write(fd, (s + "\r").encode()); time.sleep(0.4)

# wake + see what's there
send("")
out = drain(1.5)
# if at login prompt, authenticate
if "login:" in out or "ogin:" in out:
    send("radxa"); time.sleep(0.6); drain(1.0)
    send("radxa"); time.sleep(1.2); drain(1.5)
else:
    # maybe already logged in, or needs a nudge
    send(""); drain(0.8)

# run the query (markers to bracket output)
send("echo IP_START; ip -br addr show; echo IP_END")
time.sleep(1.0)
result = drain(2.5)
print("=== serial output ===")
print(result)
os.close(fd)
