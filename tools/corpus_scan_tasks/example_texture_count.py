"""Minimal real ScanTask, meant as a copy-paste starting point.

This is deliberately trivial (counts M2Texture records per file, flags
files with zero) -- the point isn't the check itself, it's showing exactly
how little a task-assignment module needs: GLOB_PATTERNS, FIELDNAMES, and
one pure analyze(path) function. Everything else (discovery, worker pool
sizing/parallelism, CSV/log output, per-file error isolation, throughput
reporting) is corpus_scan_framework.py's job, not this file's.

Run it against the real corpus with:

    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.example_texture_count:TextureCountTask \\
        --root /media/luna/data/wow_export --output-stem texture_count_demo \\
        --limit 2000   # drop --limit for a real full-corpus run
"""

import struct
from pathlib import Path

_M2_TEXTURES_OFFSET = 0x050
_TEXTURE_STRIDE = 0x10
_MIN_HEADER_SIZE = 0x0A0


def _extract_md20_blob(data: bytes) -> bytes | None:
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
            return None
        if ctag == b"MD21":
            return data[payload_start:payload_start + csize]
        pos = payload_start + csize
    return None


class TextureCountTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["texture_count"]
    PARALLEL_MODE = "process"  # no subprocess calls here, but real struct-unpack CPU work

    @staticmethod
    def analyze(path: Path) -> dict | None:
        try:
            data = path.read_bytes()
        except OSError:
            return None
        blob = _extract_md20_blob(data)
        if blob is None or len(blob) < _MIN_HEADER_SIZE or blob[0:4] != b"MD20":
            return None
        count, off = struct.unpack_from("<II", blob, _M2_TEXTURES_OFFSET)
        if count != 0 and (off > len(blob) or count > (len(blob) - off) // _TEXTURE_STRIDE):
            return None  # header claims more textures than the file actually holds
        if count > 0:
            return None  # only report the interesting (zero-texture) case
        return {"texture_count": 0}

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        return [f"{len(rows)} / {total_files} .m2 files have zero M2Texture records "
                f"({len(rows) / total_files:.3%} of the corpus)"]
