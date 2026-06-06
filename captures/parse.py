#!/usr/bin/env python3
"""Decode an esphome-logs capture of the VEVOR dehumidifier display bus.

Usage: parse.py captures/NN_label.log
Collapses identical consecutive RESPs into a single line for readability.
"""
import sys, re

NAMES = {0: "POWER", 1: "SETPT", 5: "MODE"}

prev = None
for line in open(sys.argv[1]):
    m = re.search(r"\[(\d\d:\d\d:\d\d\.\d+)\].*\[disp([AB]).*B:\s*([0-9A-Fa-f ]+?)\s*$", line)
    if not m:
        continue
    ts, dirn, frame = m.groups()
    b = frame.split()
    kind = None
    if dirn == "B" and len(b) >= 8 and b[1] == "03":
        kind = "POLL"
    elif dirn == "B" and len(b) >= 11 and b[1] == "10":
        addr = int(b[2] + b[3], 16)
        val = int(b[7] + b[8], 16)
        n = NAMES.get(addr, f"a{addr}")
        kind = f"WRITE {n}={val}"
    elif dirn == "A" and len(b) >= 23 and b[1] == "03":
        sp = int(b[3] + b[4], 16)
        rh = int(b[5] + b[6], 16)
        ta = int(b[7] + b[8], 16)
        co = int(b[11] + b[12], 16)
        s6 = b[15] + b[16]
        s7 = b[17] + b[18]
        s8 = b[19] + b[20]
        extras = ""
        if s7 != "0000":
            extras += f" s7=0x{s7}"
        if s8 != "0000":
            extras += f" s8=0x{s8}"
        kind = f"RESP sp={sp:2d} rh={rh:2d} Ta={ta:2d}C coil={co:2d}C s6=0x{s6}{extras}"
        if prev == kind:
            continue
        prev = kind
    elif dirn == "A" and len(b) >= 8 and b[1] == "0A":
        addr = int(b[2] + b[3], 16)
        kind = f"ACK a={addr}"
    else:
        kind = f"OTHER {dirn}: {frame}"
    print(f"{ts} {dirn} {kind}")
