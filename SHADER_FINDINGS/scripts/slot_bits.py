import json, sys, re, collections, statistics, os
d = sys.argv[1]
m = json.load(open(os.path.join(d, "manifest.json")))
slots = m["slots"]
nbits = (len(slots) - 1).bit_length()
blobfile = {}
for i, b in enumerate(m["blobs"]):
    blobfile[i] = b["file"]
def feats(bi):
    p = os.path.join(d, "asm", "%04d.asm" % bi)
    L = open(p).read().splitlines()
    decl = set(re.sub(r"\[\d+\]", "[N]", l) for l in L if l.startswith("dcl_") and not l.startswith("dcl_temps"))
    ops = collections.Counter(l.split()[0] for l in L[1:] if l.strip() and not l.startswith("dcl_"))
    return decl, ops, sum(ops.values())
F = {bi: feats(bi) for bi in set(x for x in slots if x is not None)}
for b in range(nbits):
    add = collections.Counter(); rem = collections.Counter(); dl = []; opd = collections.Counter(); n = 0; ch = 0
    for i in range(len(slots)):
        if i >> b & 1: continue
        j = i | 1 << b
        if j >= len(slots) or slots[i] is None or slots[j] is None: continue
        n += 1
        if slots[i] == slots[j]: continue
        ch += 1
        a, c = F[slots[i]], F[slots[j]]
        for x in c[0] - a[0]: add[x] += 1
        for x in a[0] - c[0]: rem[x] += 1
        dl.append(c[2] - a[2])
        opd.update(c[1]); opd.subtract(a[1])
    if not n: print(f"bit {b}: never toggles among compiled slots"); continue
    print(f"bit {b}: pairs {n} changed {ch}  instr delta median {statistics.median(dl) if dl else 0} range {min(dl, default=0)}..{max(dl, default=0)}")
    for x, k in add.most_common(8): print(f"   + {k:4d} {x}")
    for x, k in rem.most_common(8): print(f"   - {k:4d} {x}")
    top = [(o, v / max(ch, 1)) for o, v in opd.items() if abs(v) / max(ch, 1) >= 1]
    top.sort(key=lambda t: -abs(t[1]))
    print("   ops/pair:", ", ".join(f"{o}{v:+.1f}" for o, v in top[:12]))
