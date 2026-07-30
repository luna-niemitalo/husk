#!/usr/bin/env python3
"""Scans a real M2 corpus for files whose bone array has more than one root
bone (M2CompBone.parentBone == -1) -- the corpus-scale measurement behind
the multi-root-bone-forest glTF representation gap (see DESIGN.md's Key
design decisions). Writes matching paths, one per line, to
multiroot_skeleton_files_for_exploration.txt (repo root), same plain format
as phys_files_for_exploration.txt.

Independent of husk's own code (same "second opinion" discipline
corpus_checks.py's read_m2_header_summary uses) -- reads the M2 bone
array's raw bytes directly, not through husk. The MD20/MD21 chunk-
container walk below is intentionally duplicated from
corpus_checks.py's read_m2_header_summary rather than imported: this is a
small, self-contained, one-off exploration tool, not something that should
grow a dependency on that module's internals.

Usage:
	direnv exec . uv run --python tools/venv/bin/python tools/find_multiroot_skeletons.py [corpus_root]
"""

import struct
import sys
from pathlib import Path

try:
	from tqdm import tqdm
except ImportError:
	tqdm = None

DEFAULT_ROOT = Path("/media/luna/data/wow_export")
OUTPUT_PATH = Path(__file__).resolve().parent.parent / "multiroot_skeleton_files_for_exploration.txt"

# Same fixed field offsets husk's own src/m2.cpp / src/cmd_export.cpp use.
_M2_BONES_OFFSET = 0x02C  # M2Array<M2CompBone>: count (u32) + offset (u32)
_M2_MIN_HEADER_SIZE = 0x090
_M2_BONE_STRIDE = 0x58  # M2CompBone record size (88 bytes)
_M2_BONE_PARENT_OFFSET = 0x08  # int16_t parentBone within one bone record


def extract_md20_blob(data: bytes) -> bytes | None:
	"""Same MD20/MD21 chunk-container walk as corpus_checks.py's
	read_m2_header_summary. Returns None if the file isn't recognizable as
	MD20/MD21-wrapped, or the chunk container is truncated.
	"""
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


def count_bone_roots(m2_path: Path) -> int | None:
	"""How many of this M2's bones have parentBone == -1, or None if the
	file couldn't even be read as a real M2 (0-byte, truncated, corrupted
	-- known corpus-extraction gaps, not this script's
	concern -- or a bone array descriptor that doesn't fit the file, same
	"don't trust foreign data's own claims" bounds check husk itself
	applies).
	"""
	try:
		data = m2_path.read_bytes()
	except OSError:
		return None
	blob = extract_md20_blob(data)
	if blob is None or len(blob) < _M2_MIN_HEADER_SIZE or blob[0:4] != b"MD20":
		return None
	bone_count, bone_off = struct.unpack_from("<II", blob, _M2_BONES_OFFSET)
	if bone_count == 0:
		return 0
	end = bone_off + bone_count * _M2_BONE_STRIDE
	if end > len(blob):
		return None  # bone array claims more than the file actually holds
	roots = 0
	for i in range(bone_count):
		parent = struct.unpack_from(
			"<h", blob, bone_off + i * _M2_BONE_STRIDE + _M2_BONE_PARENT_OFFSET)[0]
		if parent == -1:
			roots += 1
	return roots


def main() -> int:
	root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_ROOT
	print(f"scanning {root} for .m2 files")
	m2_files = sorted(root.rglob("*.m2"))
	print(f"found {len(m2_files)} .m2 files")

	multiroot: list[str] = []
	unreadable = 0
	iterator = tqdm(m2_files, desc="checking bone roots", unit="file") if tqdm else m2_files
	for path in iterator:
		roots = count_bone_roots(path)
		if roots is None:
			unreadable += 1
		elif roots > 1:
			multiroot.append(str(path))

	OUTPUT_PATH.write_text("\n".join(multiroot) + ("\n" if multiroot else ""))
	print(f"{len(multiroot)} / {len(m2_files)} .m2 files have more than one root bone "
	      f"({unreadable} unreadable/skipped)")
	print(f"written to {OUTPUT_PATH}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
