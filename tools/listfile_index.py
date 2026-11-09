"""FileDataID -> listfile path for scan tasks, without parsing the CSV per process.

husk keeps a sorted, mmap-able copy of the listfile in its cache dir
(src/listfile_cache.cpp, built by any husk run given --listfile). Every
process that maps it shares the same physical pages, so a scan with N
worker processes costs one page-cached file instead of N 2.2M-entry dicts.

When that cache is missing, stale, or of another format version, this falls
back to parsing the CSV once per process, with a warning: correct, just slow.
"""

from __future__ import annotations

import bisect
import functools
import mmap
import os
import struct
import sys
import tempfile
from pathlib import Path

# Mirrors CommonHeader + SortedHeader and kSortedMagic/kFormatVersion in
# src/listfile_mmap_index.cpp, and kSortedCacheFileName in
# src/listfile_cache.cpp. Change them together; the version check below
# turns a C++-side bump into the CSV fallback, never a misread.
_HEADER = struct.Struct("<8sIIQqIIQQQQQ")  # ..., fdids, cumOffsets, blob, pathOrder offsets
_MAGIC = b"HUSKLFA1"
_FORMAT_VERSION = 2
_CACHE_FILE = "listfile.sorted.bin"


LISTFILES_DIR = Path(__file__).resolve().parent.parent / "development" / "listfiles"


def latest_listfile(path: Path) -> Path:
    """A directory stands for its alphabetically last *.csv (dated snapshots,
    community-listfile-YYYY-MM-DD.csv); a file passes through. Same rule as
    latestListfileInDirectory() in src/cli_args.cpp."""
    if not path.is_dir():
        return path
    csvs = sorted(p for p in path.iterdir() if p.suffix == ".csv")
    if not csvs:
        raise FileNotFoundError(f"expected a listfile *.csv in directory {path}, got none")
    return csvs[-1]


def cache_dir() -> Path:
    """Same lookup order as listfileCacheDir() in src/listfile_cache.cpp."""
    if override := os.environ.get("HUSK_CACHE_DIR"):
        return Path(override)
    if xdg := os.environ.get("XDG_CACHE_HOME"):
        return Path(xdg) / "husk"
    if home := os.environ.get("HOME"):
        return Path(home) / ".cache" / "husk"
    return Path(tempfile.gettempdir()) / "husk"


class _MappedListfile:
    def __init__(self, data: mmap.mmap, entry_count: int, fdids_offset: int, cum_offsets_offset: int,
                 blob_offset: int, blob_size: int):
        self._data = data
        self._fdids = memoryview(data)[fdids_offset:fdids_offset + 4 * entry_count].cast("I")
        self._offsets = memoryview(data)[cum_offsets_offset:cum_offsets_offset + 8 * (entry_count + 1)].cast("Q")
        self._blob = memoryview(data)[blob_offset:blob_offset + blob_size]

    def __len__(self) -> int:
        return len(self._fdids)

    def get(self, fdid: int, default: str | None = None) -> str | None:
        i = bisect.bisect_left(self._fdids, fdid)
        if i == len(self._fdids) or self._fdids[i] != fdid:
            return default
        return bytes(self._blob[self._offsets[i]:self._offsets[i + 1]]).decode("utf-8", errors="replace")


def _parse_csv(csv: Path) -> dict[int, str]:
    table: dict[int, str] = {}
    with csv.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            fdid_str, _, rel_path = line.partition(";")
            if not rel_path:
                continue
            try:
                table[int(fdid_str)] = rel_path.rstrip("\r\n")
            except ValueError:
                continue
    return table


def _why_unusable(cache: Path, csv: Path, header: tuple, stored_path: str) -> str | None:
    magic, version, _, source_size = header[:4]
    if magic != _MAGIC or version != _FORMAT_VERSION:
        return f"expected magic {_MAGIC!r} version {_FORMAT_VERSION}, got {magic!r} version {version}"
    if Path(stored_path).resolve() != csv.resolve():
        return f"expected a cache of {csv.resolve()}, got one of {stored_path}"
    if source_size != csv.stat().st_size:
        return f"expected source size {csv.stat().st_size}, got {source_size}"
    # husk's own check compares libstdc++ mtime ticks; a cache written after
    # the CSV's last change is the same freshness guarantee without them.
    if cache.stat().st_mtime < csv.stat().st_mtime:
        return "the CSV changed after the cache was built"
    return None


@functools.lru_cache(maxsize=4)
def open_listfile(csv: Path) -> _MappedListfile | dict[int, str]:
    """A read-only FileDataID -> path table with .get(), once per process.

    `csv` may be a directory of snapshots (see latest_listfile). An empty
    dict when it doesn't exist, matching the loaders this replaced.
    """
    if not csv.exists():
        return {}
    csv = latest_listfile(csv)
    cache = cache_dir() / _CACHE_FILE
    reason = "no cache at " + str(cache)
    if cache.exists():
        with cache.open("rb") as f:
            data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        header = _HEADER.unpack_from(data, 0)
        stored_path = bytes(data[_HEADER.size:_HEADER.size + header[5]]).decode("utf-8", errors="replace")
        reason = _why_unusable(cache, csv, header, stored_path)
        if reason is None:
            entry_count, blob_size, fdids_offset, cum_offsets_offset, blob_offset = header[6:11]
            return _MappedListfile(data, entry_count, fdids_offset, cum_offsets_offset, blob_offset, blob_size)
        data.close()
    print(f"listfile_index: parsing {csv} in this process ({reason}); "
          f"run any husk command with --listfile {csv} to rebuild the shared cache", file=sys.stderr)
    return _parse_csv(csv)
