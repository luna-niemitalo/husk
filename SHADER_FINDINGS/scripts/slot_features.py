"""Print one line per slot: program, instruction count, textures, cbuffers, inputs.

Used to find the digits of mixed-radix permutation keys by eye.
"""
import json
import re
import sys

d = sys.argv[1]
slots = json.load(open(d + "/manifest.json"))["slots"]
for i, b in enumerate(slots):
    if b is None:
        continue
    t = open(f"{d}/asm/{b:04d}.asm").read()
    tex = ",".join(re.findall(r"dcl_resource_\w+ [^\n]*?t(\d+)", t))
    cbs = ",".join(re.findall(r"dcl_constantBuffer (cb\d+\[\d+\])", t))
    ins = ",".join(re.findall(r"dcl_input_ps[^\n]* (v\d+\.\w+)", t))
    outs = ",".join(re.findall(r"dcl_output (o\d+\.\w+)", t))
    n = len([l for l in t.splitlines()[1:] if l.strip() and not l.startswith("dcl")])
    print(f"{i:3d} b{b:<3d} {n:4d} t[{tex}] {cbs} in[{ins}] out[{outs}]")
