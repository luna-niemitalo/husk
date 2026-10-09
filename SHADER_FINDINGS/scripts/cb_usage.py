import collections, glob, json, os, re, sys

root = sys.argv[1]
out = sys.argv[2]
DECL = re.compile(r"^dcl_constantBuffer cb(\d+)\[(\d+)\]")
IMM = re.compile(r"\bcb(\d+)\[(\d+)\]")
DYN = re.compile(r"\bcb(\d+)\[r\d+\.[xyzw] \+ (\d+)\]")

def scan(path):
    decl, imm, dyn = {}, collections.defaultdict(set), collections.defaultdict(set)
    for line in open(path):
        m = DECL.match(line)
        if m:
            decl[int(m[1])] = int(m[2])
            continue
        if line.startswith("dcl_"):
            continue
        for b, k in IMM.findall(line):
            imm[int(b)].add(int(k))
        for b, k in DYN.findall(line):
            dyn[int(b)].add(int(k))
    return decl, imm, dyn

containers = {}
for d in sorted(glob.glob(os.path.join(root, "*", "dx_5_0", "*"))):
    files = sorted(glob.glob(os.path.join(d, "asm", "*.asm")))
    if not files:
        continue
    size = collections.defaultdict(int)
    nprog = collections.Counter()
    imm = collections.defaultdict(set)
    dyn = collections.defaultdict(set)
    for f in files:
        dc, im, dy = scan(f)
        for b, n in dc.items():
            size[b] = max(size[b], n)
            nprog[b] += 1
        for b, s in im.items():
            imm[b] |= s
        for b, s in dy.items():
            dyn[b] |= s
    containers[os.path.relpath(d, root)] = {
        "programs": len(files),
        "cb": {
            b: {"size": size[b], "programs": nprog[b], "imm": sorted(imm[b]), "dyn_bases": sorted(dyn[b])}
            for b in sorted(size)
        },
    }

json.dump(containers, open(out, "w"), indent=1)

for stage in ("vertex", "pixel", "compute", "geometry"):
    cs = {k: v for k, v in containers.items() if k.startswith(stage + "/")}
    print(f"## {stage}: {len(cs)} containers")
    per = collections.defaultdict(list)
    for name, c in cs.items():
        for b, info in c["cb"].items():
            per[b].append((name, info))
    for b in sorted(per):
        sizes = collections.Counter(i["size"] for _, i in per[b])
        print(f"  cb{b}: in {len(per[b])} containers; sizes {sizes.most_common(6)}")
