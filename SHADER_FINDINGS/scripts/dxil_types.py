"""Collect resource type names and struct layouts from the dx_6_0 DXIL listings.

DXIL keeps the HLSL type names of constant buffers, structured buffers and their structs,
though the variable names are stripped. Output: one JSON object with
  bindings: {kind: {"<type>@<reg>": {"size": n, "containers": [...]}}}
  types:    {"<type name>": ["<llvm body>", ...]}  (every distinct body)
"""
import json
import os
import re
import sys

root, out = sys.argv[1], sys.argv[2]
res_re = re.compile(r'^!\d+ = !\{i32 \d+, (%[^*]+)\* undef, !"[^"]*", i32 (\d+), i32 (\d+), i32 (\d+), i32 (\d+)')
type_re = re.compile(r'^(%(?:"[^"]+"|[\w.]+)) = type (.*)$')
bindings, types = {}, {}
for dirpath, _, files in os.walk(root):
    if "dx_6_0" not in dirpath:
        continue
    lls = [f for f in files if f.endswith(".ll")]
    if not lls:
        continue
    container = os.path.relpath(os.path.dirname(dirpath), root)
    for f in lls:
        for line in open(os.path.join(dirpath, f), errors="replace"):
            m = type_re.match(line)
            if m:
                name = m.group(1).strip('%"')
                if not name.startswith("dx.types"):
                    body = m.group(2).strip()
                    if body not in types.setdefault(name, []):
                        types[name].append(body)
                continue
            m = res_re.match(line)
            if m:
                tname = m.group(1).strip('%"')
                space, reg, count, kind_or_size = m.group(2), int(m.group(3)), m.group(4), int(m.group(5))
                key = f"{tname}@{reg}" + (f"/space{space}" if space != "0" else "")
                kind = "cbuffer" if tname.startswith(("cb_", "hostlayout.cb", "cbuffer")) or "cb_" in tname else "resource"
                e = bindings.setdefault(kind, {}).setdefault(key, {"size_or_kind": kind_or_size, "containers": set()})
                e["containers"].add(container)
for kind in bindings.values():
    for e in kind.values():
        e["containers"] = sorted(e["containers"])
json.dump({"bindings": bindings, "types": types}, open(out, "w"), indent=1, sort_keys=True)
print(sum(len(k) for k in bindings.values()), "bindings,", len(types), "types")
