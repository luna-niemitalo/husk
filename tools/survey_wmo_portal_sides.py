#!/usr/bin/env python3
"""Surveys what WMO MOPR `side` means, on a seeded sample of WMO roots --
the measurement behind WIKI_FINDINGS/WORLD.md's MOPR `side` rule.

Per MOPR link it places the owning group on one side of the portal plane
(dot(normal, p) + distance) by three tests and compares each with `side`:
- the group's vertex centroid (fails on exterior groups, which wrap the
  building, and on concave groups);
- local geometry: the 16 off-plane vertices nearest the portal centre
  (majority sign), then triangles within two portal radii, then the centroid;
- whole-group separation: >= 90% of the group's off-plane vertices on one side.

Independent of husk's own code (raw chunk bytes, no husk parser), like
find_multiroot_skeletons.py. Writes one CSV row per link and prints the
summary counts.

Usage:
	tools/venv/bin/python tools/survey_wmo_portal_sides.py <listfile.csv> <out.csv> [n_roots] [seed] [corpus_root]
"""

import csv
import random
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

DEFAULT_ROOT = Path("/media/luna/wow_export/live")
GROUP_FILE = re.compile(r"_\d{3}\.wmo$|_lod\d\.wmo$")
# Plane distances under 1 cm count as "on the plane" (portal frames, door jambs).
ON_PLANE = 0.01
CLEAN = 0.9


def chunks(buf, start=0, end=None):
	end = len(buf) if end is None else end
	off = start
	while off + 8 <= end:
		cid = buf[off:off + 4][::-1].decode("latin1")
		size = struct.unpack_from("<I", buf, off + 4)[0]
		yield cid, off + 8, size
		off += 8 + size


def sign(v):
	return 1 if v > ON_PLANE else (-1 if v < -ON_PLANE else 0)


def read_group(buf):
	for cid, off, size in chunks(buf):
		if cid != "MOGP":
			continue
		portal_start, portal_count = struct.unpack_from("<HH", buf, off + 0x24)
		verts = tris = None
		# MOGP's header is 0x44 bytes; its sub-chunks follow inside it.
		for sid, soff, ssize in chunks(buf, off + 0x44, off + size):
			if sid == "MOVT":
				verts = np.frombuffer(buf, "<f4", ssize // 4, soff).reshape(-1, 3).astype(np.float64)
			elif sid == "MOVI":
				tris = np.frombuffer(buf, "<u2", ssize // 2, soff).reshape(-1, 3)
		return portal_start, portal_count, verts, tris
	raise ValueError("expected a MOGP chunk, found none")


def local_side(verts, tris, normal, dist, center, radius):
	d = verts @ normal + dist
	off = np.abs(d) > ON_PLANE
	if off.any():
		k = np.argsort(np.linalg.norm(verts[off] - center, axis=1))[:16]
		s = sign(float(np.sum(np.sign(d[off][k]))))
		if s:
			return s
	if tris is not None and len(tris):
		tv = verts[tris]
		cen = tv.mean(axis=1)
		near = np.linalg.norm(cen - center, axis=1) < 2 * radius + 1
		td = cen[near] @ normal + dist
		area = np.linalg.norm(np.cross(tv[near, 1] - tv[near, 0], tv[near, 2] - tv[near, 0]), axis=1)
		keep = np.abs(td) > ON_PLANE
		s = sign(float(np.sum(np.sign(td[keep]) * area[keep])))
		if s:
			return s
	return sign(float(d.mean()))


def survey_root(root_path, by_fdid, rows):
	buf = root_path.read_bytes()
	root = {cid: buf[off:off + size] for cid, off, size in chunks(buf)}
	if len(root.get("MOPR", b"")) == 0:
		return False
	n_groups = struct.unpack_from("<I", root["MOHD"], 4)[0]
	group_flags = [struct.unpack_from("<I", root["MOGI"], i * 32)[0] for i in range(n_groups)]
	mopv = np.frombuffer(root["MOPV"], "<f4").reshape(-1, 3).astype(np.float64)
	mopt = [struct.unpack_from("<HH4f", root["MOPT"], i * 20) for i in range(len(root["MOPT"]) // 20)]
	mopr = [struct.unpack_from("<HHhH", root["MOPR"], i * 8) for i in range(len(root["MOPR"]) // 8)]
	gfid = struct.unpack_from(f"<{len(root['GFID']) // 4}I", root["GFID"]) if "GFID" in root else None
	groups = []
	for g in range(n_groups):
		path = by_fdid / str(gfid[g]) if gfid else Path(re.sub(r"\.wmo$", f"_{g:03d}.wmo", str(root_path)))
		groups.append(read_group(path.read_bytes()))
	owner = {r: g for g, (start, count, _, _) in enumerate(groups) for r in range(start, start + count) if count}
	for r, (portal, other, side, _) in enumerate(mopr):
		g = owner[r]
		start, count, nx, ny, nz, dist = mopt[portal]
		normal = np.array([nx, ny, nz])
		poly = mopv[start:start + count]
		center = poly.mean(axis=0)
		radius = float(np.max(np.linalg.norm(poly - center, axis=1)))
		_, _, verts, tris = groups[g]
		d = verts @ normal + dist
		off = np.abs(d) > ON_PLANE
		rows.append({
			"root": str(root_path), "ref": r, "portal": portal, "group": g, "other": other, "side": side,
			"exterior": int(bool(group_flags[g] & 0x8)),
			"centroid": sign(float(d.mean())),
			"local": local_side(verts, tris, normal, dist, center, radius),
			"frac_pos": float(np.mean(d[off] > 0)) if off.any() else 0.0,
			"frac_neg": float(np.mean(d[off] < 0)) if off.any() else 0.0,
		})
	return True


def summarize(rows):
	n = len(rows)
	print(f"links {n}, portals {len({(r['root'], r['portal']) for r in rows})}, roots {len({r['root'] for r in rows})}")
	centroid_miss = [r for r in rows if r["centroid"] != r["side"]]
	print(f"centroid agrees {n - len(centroid_miss)}/{n}; misses from exterior groups "
		  f"{sum(r['exterior'] for r in centroid_miss)}/{len(centroid_miss)} "
		  f"(exterior groups own {sum(r['exterior'] for r in rows)}/{n} links)")
	by_portal = defaultdict(list)
	for r in rows:
		by_portal[(r["root"], r["portal"])].append(r)
	pairs = [refs for refs in by_portal.values() if len(refs) == 2]
	print(f"two-ref portals {len(pairs)}: sides opposite {sum(a['side'] == -b['side'] for a, b in pairs)}, "
		  f"each names the other's group {sum(a['other'] == b['group'] and b['other'] == a['group'] for a, b in pairs)}")
	local = Counter()
	clean = Counter()
	for a, b in pairs:
		pos = a if a["side"] == 1 else b
		neg = b if pos is a else a
		if pos["local"] and neg["local"] and pos["local"] != neg["local"]:
			local["agree" if pos["local"] == 1 else "flipped"] += 1
		else:
			local["not separating"] += 1
		whole = [1 if r["frac_pos"] >= CLEAN else (-1 if r["frac_neg"] >= CLEAN else 0) for r in (pos, neg)]
		if whole[0] and whole[1] and whole[0] != whole[1]:
			clean["agree" if whole[0] == 1 else "flipped"] += 1
		else:
			clean["not clean"] += 1
	print(f"local geometry, per portal: {dict(local)}")
	print(f"whole-group separation >= {CLEAN:.0%}, per portal: {dict(clean)}")
	singles = [refs[0] for refs in by_portal.values() if len(refs) == 1]
	print(f"single-ref portals: local agrees {sum(r['local'] == r['side'] for r in singles)}/{len(singles)}")


def main():
	listfile, out = sys.argv[1], sys.argv[2]
	n = int(sys.argv[3]) if len(sys.argv) > 3 else 4000
	seed = int(sys.argv[4]) if len(sys.argv) > 4 else 20261124
	corpus = Path(sys.argv[5]) if len(sys.argv) > 5 else DEFAULT_ROOT
	roots = sorted({
		p for p in (line.strip().partition(";")[2].lower() for line in open(listfile, encoding="utf-8", errors="replace"))
		if p.startswith("world/wmo/") and p.endswith(".wmo") and not GROUP_FILE.search(p)
	})
	sample = random.Random(seed).sample(roots, min(n, len(roots)))
	rows = []
	missing = 0
	for i, p in enumerate(sample):
		try:
			survey_root(corpus / p, corpus / "_by_fdid", rows)
		except FileNotFoundError:
			missing += 1
		if i % 500 == 0:
			print(f"{i}/{len(sample)} roots, {len(rows)} links, {missing} missing", file=sys.stderr, flush=True)
	with open(out, "w", newline="") as fh:
		w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
		w.writeheader()
		w.writerows(rows)
	print(f"sample {len(sample)} of {len(roots)} roots (seed {seed}), {missing} missing from the corpus")
	summarize(rows)


if __name__ == "__main__":
	main()
