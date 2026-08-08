#!/usr/bin/env python3
"""Scans a real M2 corpus for files where more than one M2Texture record
shares the same `type` -- the collision case wowdev.wiki's own
"Replacable texture lookup" section calls out by name ("a model can have
more than one [HARDCODED texture] of course. Its just the last one written
to the file"), and the concrete question this tool exists to answer: given
that husk's own `husk info` now dereferences `textureLookup`
(TODO/TODO_correctness.md's former item 2), does that reverse lookup table ever
carry real information beyond what each texture's own `type` field already
says forward -- and if so, how much of the real corpus does it actually
touch?

For every file with a collision, also resolves the same-basename .skin
sibling's batches (materialIndex/textureComboIndex -> M2's own
textureCombos -> a specific texture index -- the real, deterministic
per-material texture choice a husk export actually makes) and checks
whether each affected batch's own resolved texture agrees with what
`textureLookup` would have picked for that type. A batch that *disagrees*
is the real headline number: proof a consumer trusting `textureLookup`
alone (instead of the batch-specific textureCombos chain husk's own
cmd_export.cpp already uses) would silently get the wrong texture for that
specific material.

Independent of husk's own code (same "second opinion" discipline
corpus_checks.py's read_m2_header_summary and every other find_*.py tool
here uses) -- reads M2/.skin bytes directly, not through husk. The
MD20/MD21 chunk-container walk and same-basename .skin lookup are
duplicated from find_texture_transform_files.py rather than imported, same
"small, self-contained, one-off exploration tool" precedent.

Usage:
	direnv exec . uv run --python tools/venv/bin/python tools/find_texture_type_collisions.py [corpus_root]

Writes texture_type_collisions.csv (one row per M2 file with >=1 real
collision) and texture_type_collisions_scan.log (human-readable summary,
same content as the final stdout block) to the repo root, same
CSV+summary-log pairing as m2_chunk_discovery.csv/m2_chunk_discovery_stderr.log.
"""

import csv
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

try:
	from tqdm import tqdm
except ImportError:
	tqdm = None

DEFAULT_ROOT = Path("/media/luna/data/wow_export")
REPO_ROOT = Path(__file__).resolve().parent.parent
OUTPUT_CSV = REPO_ROOT / "texture_type_collisions.csv"
OUTPUT_LOG = REPO_ROOT / "texture_type_collisions_scan.log"

# Same fixed field offsets husk's own src/m2_header.cpp / src/m2_primitives.cpp use.
_M2_TEXTURES_OFFSET = 0x050        # M2Array<M2Texture>: count (u32) + offset (u32)
_M2_TEXTURE_LOOKUP_OFFSET = 0x068  # M2Array<uint16_t> ("Replacable texture lookup")
_M2_TEXTURE_COMBOS_OFFSET = 0x080  # M2Array<uint16_t> ("Texture lookup table" / batch combiner data)
_M2_MIN_HEADER_SIZE = 0x0A0
_TEXTURE_STRIDE = 0x10  # M2Texture record size (16 bytes): type(u32) + flags(u32) + filename(M2Array<char>)

# .skin (always flat, SKIN magic -- src/skin.cpp)
_SKIN_BATCHES_OFFSET = 0x24
_SKIN_BATCH_STRIDE = 0x18
_SKIN_BATCH_MATERIAL_INDEX_OFFSET = 0x0A
_SKIN_BATCH_TEXTURE_COMBO_INDEX_OFFSET = 0x10


def extract_md20_blob(data: bytes) -> bytes | None:
	"""Same MD20/MD21 chunk-container walk as every other find_*.py tool here."""
	if len(data) < 8:
		return None
	if data[0:4] == b"MD20":
		return data
	pos = 0
	while pos + 8 <= len(data):
		ctag = data[pos:pos + 4]
		csize = struct.unpack_from("<I", data, pos + 4)[0]
		payload_start = pos + 8
		if payload_start + csize > len(data):
			return None  # truncated chunk container
		if ctag == b"MD21":
			return data[payload_start:payload_start + csize]
		pos = payload_start + csize
	return None


def read_array(blob: bytes, off: int) -> tuple[int, int] | None:
	if off + 8 > len(blob):
		return None
	return struct.unpack_from("<II", blob, off)


def read_uint16_array(blob: bytes, off: int) -> list[int] | None:
	arr = read_array(blob, off)
	if arr is None:
		return None
	count, data_off = arr
	if count == 0:
		return []
	if data_off > len(blob) or count > (len(blob) - data_off) // 2:
		return None  # claims more than the file actually holds
	return list(struct.unpack_from(f"<{count}H", blob, data_off))


def read_texture_types(blob: bytes) -> list[int] | None:
	arr = read_array(blob, _M2_TEXTURES_OFFSET)
	if arr is None:
		return None
	count, off = arr
	if count == 0:
		return []
	if off > len(blob) or count > (len(blob) - off) // _TEXTURE_STRIDE:
		return None
	return [struct.unpack_from("<I", blob, off + i * _TEXTURE_STRIDE)[0] for i in range(count)]


def find_same_basename_skins(m2_path: Path) -> list[Path]:
	"""Same-basename numeric-suffix .skin lookup, preferring an exactly
	2-digit suffix -- see find_texture_transform_files.py's identical helper
	for the full rationale (WoW's own real naming convention, cmd_export.cpp's
	findSameBasenameSkins). Described here rather than imported, matching
	this repo's own self-contained-one-off-tool precedent.
	"""
	basename = m2_path.stem
	candidates = list(m2_path.parent.glob(f"{basename}*.skin"))
	suffix_re = re.compile(r"^(\d+)\.skin$", re.IGNORECASE)
	two_digit = []
	any_digit = []
	for c in candidates:
		rest = c.name[len(basename):]
		m = suffix_re.match(rest)
		if not m:
			continue
		any_digit.append(c)
		if len(m.group(1)) == 2:
			two_digit.append(c)
	return sorted(two_digit) if two_digit else sorted(any_digit)


def read_skin_batches(data: bytes) -> list[tuple[int, int]] | None:
	"""Returns (materialIndex, textureComboIndex) per batch, or None if the
	file isn't a readable .skin.
	"""
	if len(data) < 4 or data[0:4] != b"SKIN":
		return None
	arr = read_array(data, _SKIN_BATCHES_OFFSET)
	if arr is None:
		return None
	count, off = arr
	if count == 0:
		return []
	if off > len(data) or count > (len(data) - off) // _SKIN_BATCH_STRIDE:
		return None
	out = []
	for i in range(count):
		b_off = off + i * _SKIN_BATCH_STRIDE
		material_index = struct.unpack_from("<H", data, b_off + _SKIN_BATCH_MATERIAL_INDEX_OFFSET)[0]
		combo_index = struct.unpack_from("<H", data, b_off + _SKIN_BATCH_TEXTURE_COMBO_INDEX_OFFSET)[0]
		out.append((material_index, combo_index))
	return out


def analyze(m2_path: Path) -> dict | None:
	"""Returns None if this file has no texture-type collision at all (the
	overwhelmingly common case -- every hardcoded/named slot is normally
	unique per model). Otherwise a dict with the CSV row's worth of detail.
	"""
	try:
		data = m2_path.read_bytes()
	except OSError:
		return None
	blob = extract_md20_blob(data)
	if blob is None or len(blob) < _M2_MIN_HEADER_SIZE or blob[0:4] != b"MD20":
		return None

	types = read_texture_types(blob)
	if not types:
		return None

	by_type: dict[int, list[int]] = defaultdict(list)
	for tex_idx, t in enumerate(types):
		by_type[t].append(tex_idx)
	colliding = {t: idxs for t, idxs in by_type.items() if len(idxs) > 1}
	if not colliding:
		return None

	texture_lookup = read_uint16_array(blob, _M2_TEXTURE_LOOKUP_OFFSET) or []
	texture_combos = read_uint16_array(blob, _M2_TEXTURE_COMBOS_OFFSET) or []

	# What textureLookup would pick for each colliding type, when in range
	# and not the 0xFFFF ("no entry") sentinel.
	lookup_pick: dict[int, int] = {}
	for t in colliding:
		if t < len(texture_lookup) and texture_lookup[t] != 0xFFFF:
			lookup_pick[t] = texture_lookup[t]

	skin_paths = find_same_basename_skins(m2_path)
	batches_total = 0
	batches_on_colliding = 0
	batches_matching = 0
	batches_diverging = 0
	skin_readable = False
	if skin_paths:
		try:
			skin_data = skin_paths[0].read_bytes()
		except OSError:
			skin_data = b""
		batches = read_skin_batches(skin_data)
		if batches is not None:
			skin_readable = True
			batches_total = len(batches)
			for _material_index, combo_idx in batches:
				if combo_idx >= len(texture_combos):
					continue
				tex_idx = texture_combos[combo_idx]
				if tex_idx >= len(types):
					continue
				t = types[tex_idx]
				if t not in colliding:
					continue
				batches_on_colliding += 1
				pick = lookup_pick.get(t)
				if pick is None:
					continue
				if tex_idx == pick:
					batches_matching += 1
				else:
					batches_diverging += 1

	colliding_texture_count = sum(len(idxs) for idxs in colliding.values())
	colliding_types_str = ";".join(f"{t}:{len(idxs)}" for t, idxs in sorted(colliding.items()))

	return {
		"m2_path": str(m2_path),
		"texture_count": len(types),
		"colliding_types": colliding_types_str,
		"colliding_texture_count": colliding_texture_count,
		"has_texture_lookup": bool(texture_lookup),
		"skin_found": bool(skin_paths),
		"skin_readable": skin_readable,
		"batches_total": batches_total,
		"batches_on_colliding_type": batches_on_colliding,
		"batches_matching_texture_lookup": batches_matching,
		"batches_diverging_from_texture_lookup": batches_diverging,
	}


def main() -> int:
	root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_ROOT
	print(f"scanning {root} for .m2 files")
	m2_files = sorted(root.rglob("*.m2"))
	print(f"found {len(m2_files)} .m2 files")

	rows: list[dict] = []
	iterator = tqdm(m2_files, desc="checking texture type collisions", unit="file") if tqdm else m2_files
	scanned = 0
	for path in iterator:
		row = analyze(path)
		scanned += 1
		if row is not None:
			rows.append(row)
		if tqdm is None and scanned % 10000 == 0:
			print(f"  scanned {scanned}/{len(m2_files)} ({len(rows)} matches)")

	fieldnames = [
		"m2_path", "texture_count", "colliding_types", "colliding_texture_count",
		"has_texture_lookup", "skin_found", "skin_readable", "batches_total",
		"batches_on_colliding_type", "batches_matching_texture_lookup",
		"batches_diverging_from_texture_lookup",
	]
	with OUTPUT_CSV.open("w", newline="") as f:
		writer = csv.DictWriter(f, fieldnames=fieldnames)
		writer.writeheader()
		writer.writerows(rows)

	total_meshes = len(m2_files)
	affected_meshes = len(rows)
	affected_colliding_textures = sum(r["colliding_texture_count"] for r in rows)
	meshes_with_skin = sum(1 for r in rows if r["skin_readable"])
	batches_total = sum(r["batches_total"] for r in rows)
	batches_on_colliding = sum(r["batches_on_colliding_type"] for r in rows)
	batches_matching = sum(r["batches_matching_texture_lookup"] for r in rows)
	batches_diverging = sum(r["batches_diverging_from_texture_lookup"] for r in rows)
	meshes_with_diverging_batch = sum(1 for r in rows if r["batches_diverging_from_texture_lookup"] > 0)

	summary_lines = [
		"",
		f"{affected_meshes} / {total_meshes} .m2 files have >=1 M2Texture type collision "
		f"({affected_meshes / total_meshes:.3%} of the corpus)",
		f"{affected_colliding_textures} individual texture records are involved in some collision",
		f"{meshes_with_skin} / {affected_meshes} of those files have a resolvable same-basename "
		".skin sibling",
		f"{batches_total} total batches across those {meshes_with_skin} .skin files",
		f"{batches_on_colliding} of those batches resolve (via textureCombos) to a texture whose "
		"type collides with another texture in the same file",
		f"  -> {batches_matching} agree with what textureLookup would pick for that type",
		f"  -> {batches_diverging} DISAGREE with textureLookup's pick -- a consumer trusting "
		"textureLookup alone would get the wrong texture for these",
		f"{meshes_with_diverging_batch} distinct .m2 file(s) have at least one such diverging batch",
		f"written to {OUTPUT_CSV}",
	]
	for line in summary_lines:
		print(line)
	OUTPUT_LOG.write_text("\n".join(summary_lines) + "\n")
	print(f"summary also written to {OUTPUT_LOG}")

	return 0


if __name__ == "__main__":
	sys.exit(main())
