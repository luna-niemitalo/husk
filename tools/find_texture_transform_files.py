#!/usr/bin/env python3
"""Scans a real M2 corpus for files carrying a genuinely constant (not
animated), non-identity-rotation M2TextureTransform -- a candidate real
fixture for verifying the texture-transform pivot-correction math
(M2's rotation pivots around the texture center (0.5, 0.5); glTF's
KHR_texture_transform pivots around (0,0) -- see M2_COMPLETENESS.md's
"Texture transform (constant case)" row and src/m2.hpp's TextureTransform
doc comment). Writes matching M2 paths, one per line, to
texture_transform_files_for_exploration.txt (repo root), same plain format
as multiroot_skeleton_files_for_exploration.txt/phys_files_for_exploration.txt.

Independent of husk's own code (same "second opinion" discipline
corpus_checks.py's read_m2_header_summary uses) -- reads the M2 header's
textureTransforms array and each M2TextureTransform's raw M2Track bytes
directly, not through husk. The MD20/MD21 chunk-container walk below is
intentionally duplicated from find_multiroot_skeletons.py rather than
imported: this is a small, self-contained, one-off exploration tool.

For each hit, also looks for a same-basename .skin sibling and checks
whether any batch's textureTransformComboIndex actually resolves (via the
M2's own textureTransformCombos lookup) to that specific transform index
-- a hit with no skin reference is a dead/unreferenced array entry, a much
weaker fixture candidate than one a real batch actually uses.

Usage:
	direnv exec . uv run --python tools/venv/bin/python tools/find_texture_transform_files.py [corpus_root]
"""

import re
import struct
import sys
from pathlib import Path

try:
	from tqdm import tqdm
except ImportError:
	tqdm = None

DEFAULT_ROOT = Path("/media/luna/data/wow_export")
OUTPUT_PATH = Path(__file__).resolve().parent.parent / "texture_transform_files_for_exploration.txt"

# Same fixed field offsets husk's own src/m2.cpp uses.
_M2_TEXTURE_TRANSFORMS_OFFSET = 0x060  # M2Array<M2TextureTransform>: count (u32) + offset (u32)
_M2_TEXTURE_TRANSFORM_COMBOS_OFFSET = 0x098  # M2Array<uint16_t>
_M2_MIN_HEADER_SIZE = 0x0A0
_TRANSFORM_STRIDE = 0x3C  # M2TextureTransform record size (60 bytes)
_TRACK_TIMESTAMPS_OUTER_OFFSET = 0x04  # M2Track<T>: timestamps outer M2Array
_TRACK_VALUES_OUTER_OFFSET = 0x0C  # M2Track<T>: values outer M2Array

# .skin (always flat, SKIN magic -- src/skin.cpp)
_SKIN_BATCHES_OFFSET = 0x24
_SKIN_BATCH_STRIDE = 0x18
_SKIN_BATCH_TEXTURE_TRANSFORM_COMBO_OFFSET = 0x16


def extract_md20_blob(data: bytes) -> bytes | None:
	"""Same MD20/MD21 chunk-container walk as find_multiroot_skeletons.py."""
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


def read_constant_track_value_offset(blob: bytes, track_off: int) -> int | None:
	"""Mirrors src/m2.cpp's constantTrackValueOffset: the values-outer
	M2Array must have exactly 1 entry, whose own inner M2Array must also
	have exactly 1 entry -- anything else is empty or genuinely animated.
	"""
	outer = read_array(blob, track_off + _TRACK_VALUES_OUTER_OFFSET)
	if outer is None or outer[0] != 1:
		return None
	inner = read_array(blob, outer[1])
	if inner is None or inner[0] != 1:
		return None
	return inner[1]


def decode_transform(blob: bytes, off: int) -> dict | None:
	"""Decodes one M2TextureTransform record at byte offset `off`. Returns
	None if the record itself doesn't fit the blob. Each of
	translation/rotation/scaling is independently either a resolved
	constant value, or None (empty or genuinely animated -- this script
	doesn't distinguish the two, since neither is a usable fixture).
	"""
	if off + _TRANSFORM_STRIDE > len(blob):
		return None

	translation = None
	t_off = read_constant_track_value_offset(blob, off + 0x00)
	if t_off is not None and t_off + 12 <= len(blob):
		translation = struct.unpack_from("<fff", blob, t_off)

	rotation = None
	r_off = read_constant_track_value_offset(blob, off + 0x14)
	if r_off is not None and r_off + 16 <= len(blob):
		rotation = struct.unpack_from("<ffff", blob, r_off)

	scaling = None
	s_off = read_constant_track_value_offset(blob, off + 0x28)
	if s_off is not None and s_off + 12 <= len(blob):
		scaling = struct.unpack_from("<fff", blob, s_off)

	return {"translation": translation, "rotation": rotation, "scaling": scaling}


def is_identity_rotation(q: tuple[float, float, float, float], eps: float = 1e-4) -> bool:
	x, y, z, w = q
	return abs(x) < eps and abs(y) < eps and abs(z) < eps and abs(abs(w) - 1.0) < eps


def find_hits_in_m2(m2_path: Path) -> list[dict]:
	"""Returns a list of hit records (one per constant-non-identity-rotation
	M2TextureTransform found in this file), or [] if none / unreadable.
	"""
	try:
		data = m2_path.read_bytes()
	except OSError:
		return []
	blob = extract_md20_blob(data)
	if blob is None or len(blob) < _M2_MIN_HEADER_SIZE or blob[0:4] != b"MD20":
		return []

	arr = read_array(blob, _M2_TEXTURE_TRANSFORMS_OFFSET)
	if arr is None:
		return []
	count, base_off = arr
	if count == 0:
		return []
	if base_off > len(blob) or count > (len(blob) - base_off) // _TRANSFORM_STRIDE:
		return []  # claims more than the file holds -- corrupt/truncated, skip

	hits = []
	for i in range(count):
		off = base_off + i * _TRANSFORM_STRIDE
		xf = decode_transform(blob, off)
		if xf is None or xf["rotation"] is None:
			continue
		if is_identity_rotation(xf["rotation"]):
			continue
		hits.append({
			"transform_index": i,
			"translation": xf["translation"],
			"rotation": xf["rotation"],
			"scaling": xf["scaling"],
		})
	return hits


def find_texture_transform_combos(m2_path: Path) -> list[int] | None:
	try:
		data = m2_path.read_bytes()
	except OSError:
		return None
	blob = extract_md20_blob(data)
	if blob is None or len(blob) < _M2_MIN_HEADER_SIZE or blob[0:4] != b"MD20":
		return None
	arr = read_array(blob, _M2_TEXTURE_TRANSFORM_COMBOS_OFFSET)
	if arr is None:
		return None
	count, off = arr
	if count == 0:
		return []
	if off > len(blob) or count > (len(blob) - off) // 2:
		return None
	return list(struct.unpack_from(f"<{count}H", blob, off))


def find_same_basename_skins(m2_path: Path) -> list[Path]:
	"""Same-basename numeric-suffix .skin lookup, preferring an exactly
	2-digit suffix when one exists (WoW's own real convention, per
	CORPUS_TODO.md #3b / cmd_export.cpp's findSameBasenameSkins) -- kept as
	a fallback to any-digit-run when no 2-digit match exists at all.
	Described here rather than imported, matching this repo's own
	self-contained-one-off-tool precedent.
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


def find_skin_batch_reference(m2_path: Path, transform_index: int) -> bool:
	"""True if some batch, in some same-basename .skin sibling, has a
	textureTransformComboIndex that resolves (via the M2's own
	textureTransformCombos array) to transform_index.
	"""
	combos = find_texture_transform_combos(m2_path)
	if not combos:
		return False
	for skin_path in find_same_basename_skins(m2_path):
		try:
			data = skin_path.read_bytes()
		except OSError:
			continue
		if len(data) < 4 or data[0:4] != b"SKIN":
			continue
		arr = read_array(data, _SKIN_BATCHES_OFFSET)
		if arr is None:
			continue
		count, off = arr
		if off > len(data) or count > (len(data) - off) // _SKIN_BATCH_STRIDE:
			continue
		for i in range(count):
			b_off = off + i * _SKIN_BATCH_STRIDE
			combo_idx = struct.unpack_from(
				"<H", data, b_off + _SKIN_BATCH_TEXTURE_TRANSFORM_COMBO_OFFSET)[0]
			if combo_idx == 0xFFFF or combo_idx >= len(combos):
				continue
			if combos[combo_idx] == transform_index:
				return True
	return False


def main() -> int:
	root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_ROOT
	print(f"scanning {root} for .m2 files")
	m2_files = sorted(root.rglob("*.m2"))
	print(f"found {len(m2_files)} .m2 files")

	any_transform_count = 0
	constant_nonidentity_hits: list[tuple[Path, dict]] = []
	iterator = tqdm(m2_files, desc="checking texture transforms", unit="file") if tqdm else m2_files
	for path in iterator:
		hits = find_hits_in_m2(path)
		if hits:
			any_transform_count += 1
		for h in hits:
			constant_nonidentity_hits.append((path, h))

	skin_referenced: list[tuple[Path, dict]] = []
	for path, h in constant_nonidentity_hits:
		if find_skin_batch_reference(path, h["transform_index"]):
			skin_referenced.append((path, h))

	lines = [str(p) for p, _ in constant_nonidentity_hits]
	OUTPUT_PATH.write_text("\n".join(lines) + ("\n" if lines else ""))

	print()
	print(f"{any_transform_count} / {len(m2_files)} .m2 files have any textureTransforms entry")
	print(f"{len(constant_nonidentity_hits)} textureTransform record(s) across "
	      f"{len({p for p, _ in constant_nonidentity_hits})} file(s) are constant with a "
	      f"non-identity rotation")
	print(f"{len(skin_referenced)} of those are actually referenced by a real .skin batch")
	print(f"written to {OUTPUT_PATH}")

	if skin_referenced:
		print()
		print("skin-referenced candidates:")
		for path, h in skin_referenced[:10]:
			print(f"  {path}")
			print(f"    transform_index={h['transform_index']} rotation={h['rotation']} "
			      f"translation={h['translation']} scaling={h['scaling']}")

	return 0


if __name__ == "__main__":
	sys.exit(main())
