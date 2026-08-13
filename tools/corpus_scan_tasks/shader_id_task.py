"""Real M2Batch::shader_id corpus scan, for TODO/MULTI_TEXTURE_LAYER_TODO.md.

husk currently parses no field of M2Batch's on-disk shader_id (offset 0x02,
see src/skin.cpp's parseBatches) despite it being the one field that makes
the Cata+ combiner-formula-selection question tractable without a WotLK-era
heuristic derivation (see wowdev.wiki M2/.skin.wiki's M2GetPixelShaderID/
M2GetVertexShaderID). This task answers, over the *full* local corpus (not
a hand-picked sample): how often is the 0x8000 table-lookup path used, how
often is textureCount > 1, and how often does a real multi-texture batch
carry shader_id == 0 (which would make it un-resolvable either way).

Run against the real corpus with:

    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.shader_id_task:ShaderIdTask \\
        --root /media/luna/data/wow_export --output-stem shader_id_corpus
"""

import struct
from pathlib import Path

_BATCH_STRIDE = 0x18
_SHADER_ID_OFFSET = 0x02
_TEXTURE_COUNT_OFFSET = 0x0E


def _read_array(data: bytes, offset: int) -> tuple[int, int] | None:
    if offset + 8 > len(data):
        return None
    count, arr_offset = struct.unpack_from("<II", data, offset)
    return count, arr_offset


def _parse_batches(data: bytes) -> list[tuple[int, int]] | None:
    """[(shader_id, textureCount), ...] for one real .skin file's M2Batch array."""
    if data[:4] != b"SKIN":
        return None
    # M2SkinProfile header: magic(4) + vertices/indices/bones/submeshes (M2Array, 8 bytes each) + batches (M2Array)
    batches_field_offset = 4 + 8 * 4
    arr = _read_array(data, batches_field_offset)
    if arr is None:
        return None
    count, off = arr
    if count == 0 or off > len(data) or count > (len(data) - off) // _BATCH_STRIDE:
        return None
    out = []
    for i in range(count):
        base = off + i * _BATCH_STRIDE
        shader_id = struct.unpack_from("<H", data, base + _SHADER_ID_OFFSET)[0]
        texture_count = struct.unpack_from("<H", data, base + _TEXTURE_COUNT_OFFSET)[0]
        out.append((shader_id, texture_count))
    return out


class ShaderIdTask:
    GLOB_PATTERNS = ["*.skin"]
    FIELDNAMES = ["batches", "high_bit_batches", "tc_gt1_batches", "tc_gt1_shaderid_zero"]
    PARALLEL_MODE = "thread"  # plain struct.unpack over a read()'d file, no subprocess -- skip pickling overhead
    BATCH_SIZE = 32           # cheap per-file work; amortize per-task dispatch overhead

    @staticmethod
    def analyze(path: Path) -> dict | None:
        try:
            data = path.read_bytes()
        except OSError:
            return None
        batches = _parse_batches(data)
        if not batches:
            return None
        high_bit = sum(1 for sid, _ in batches if sid & 0x8000)
        tc_gt1 = [(sid, tc) for sid, tc in batches if tc > 1]
        tc_gt1_zero = sum(1 for sid, tc in tc_gt1 if sid == 0)
        return {
            "batches": len(batches),
            "high_bit_batches": high_bit,
            "tc_gt1_batches": len(tc_gt1),
            "tc_gt1_shaderid_zero": tc_gt1_zero,
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        total_batches = sum(r["batches"] for r in rows)
        high_bit = sum(r["high_bit_batches"] for r in rows)
        tc_gt1 = sum(r["tc_gt1_batches"] for r in rows)
        tc_gt1_zero = sum(r["tc_gt1_shaderid_zero"] for r in rows)
        lines = [
            f"{len(rows)} / {total_files} .skin files had a parseable, non-empty batch array",
            f"{total_batches} total batches",
            f"{high_bit} / {total_batches} batches ({high_bit / total_batches:.2%}) have the 0x8000 "
            f"table-lookup bit set",
            f"{tc_gt1} / {total_batches} batches ({tc_gt1 / total_batches:.2%}) have textureCount > 1",
            f"{tc_gt1_zero} / {tc_gt1} textureCount > 1 batches ({tc_gt1_zero / tc_gt1:.3%}) have "
            f"shader_id == 0 (unresolvable either way)" if tc_gt1 else "0 textureCount > 1 batches",
        ]
        return lines
