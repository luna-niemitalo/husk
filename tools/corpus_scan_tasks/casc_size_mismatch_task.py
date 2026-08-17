"""Full-corpus scan for files whose on-disk byte count doesn't match what
CASC itself reports for that same FileDataID -- the general form of the
gap `casc-tool`'s partial-encryption fallback was built to close (see that
project's README "Design notes": `dbfilesclient/item.db2` and two sibling
DB2s were each short by a few dozen bytes, a genuinely encrypted trailing
span with no public key, not a truncated download). Those three are now
fixed; this task finds whatever *else* in the corpus has the same shape.

Deliberately not a per-file `casc-tool info` subprocess (that would reopen
the whole CASC storage -- index load alone measured ~5s -- once per file,
which is a non-starter across a multi-hundred-thousand-file corpus). CASC
already hands out every FileDataID's real content size for the *entire*
storage in one shot via `casc-tool list '*' --format csv --limit 0`
(measured against the real install: ~5s, 3,237,724 rows, one storage
open) -- this task runs that once, loads the fdid/name -> size mapping
into memory, and every per-file `analyze()` call is then just a dict
lookup plus one `os.stat()`. That's why `PARALLEL_MODE = "thread"`: the
per-file work is too cheap to be worth `ProcessPoolExecutor`'s pickling/
IPC cost, and thread mode lets every worker share the one in-process
mapping instead of each process paying its own ~5s CASC-list cost (see
CORPUS_SCANS.md's own guidance on when "thread" is the right call).

Corpus files under `_unresolved/` (CascLib's own `FILE########.dat`
convention for FileDataIDs the listfile can't name -- see this project's
`missing_texture_task.py` and casc-tool's README) are matched by decoding
that hex FileDataID and looking it up directly, since they have no
listfile-resolved name to match by.

A mismatch here doesn't by itself prove corruption -- a locally newer/
older CASC build than the one the corpus was exported from would show
every file as "mismatched" against a real, unrelated content change, not
a truncation. Cross-check the reported `casc_build` in the scan log
against the corpus's own known export build before trusting a large
mismatch count.

Run with:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.casc_size_mismatch_task:CascSizeMismatchTask \\
        --root /media/luna/data/wow_export --output-stem casc_size_mismatch
"""
from __future__ import annotations

import csv
import re
import subprocess
import threading
from pathlib import Path

CASC_TOOL_BIN = Path("/home/luna/dev/casc-tool/build/casc-tool")
STORAGE = Path("/media/luna/games/World of Warcraft")
LISTFILE = Path("/media/luna/userdata/Downloads/community-listfile.csv")
KEYS = Path("/home/luna/dev/casc-tool/development/TACTKeys/WoW.txt")
CORPUS_ROOT = Path("/media/luna/data/wow_export")
LIST_TIMEOUT = 120.0

# CascLib's own convention for a FileDataID with no listfile name, e.g.
# "FILE00728761.dat" -- 8 hex digits of the FileDataID, upper or lower case.
UNRESOLVED_RE = re.compile(r"^FILE([0-9A-Fa-f]{8})\.dat$")


_load_lock = threading.Lock()
_cache: tuple[dict[str, tuple[int, int]], dict[int, int]] | None = None


def _load_casc_sizes() -> tuple[dict[str, tuple[int, int]], dict[int, int]]:
    """One `casc-tool list` call over the whole storage: name (lowercased,
    forward-slashed) -> (fdid, content size), and fdid -> content size.
    Computed once, ever, shared by every worker thread -- guarded by an
    explicit lock rather than bare `functools.lru_cache`, because
    `lru_cache` only protects its internal bookkeeping, not the wrapped
    call itself: with `PARALLEL_MODE = "thread"`, several worker threads
    reach this function within the same instant on a cold cache, and an
    unguarded `lru_cache` lets each of them see a miss and independently
    kick off its own `casc-tool list` (each one reopening the whole
    storage) before any of them finishes and populates the cache --
    measured live, this turned a single ~5s storage open into `window`
    (6-9) concurrent ones stacked on the same disk, which is what actually
    made the first smoke-test run look like a many-seconds-per-file
    bottleneck. The double-checked lock below makes the second and later
    threads block briefly on the lock and then just reuse the first
    thread's already-computed result.
    """
    global _cache
    if _cache is not None:
        return _cache
    with _load_lock:
        if _cache is None:
            p = subprocess.run(
                [str(CASC_TOOL_BIN), "list", "*", "--storage", str(STORAGE), "--listfile", str(LISTFILE),
                 "--keys", str(KEYS), "--format", "csv", "--limit", "0"],
                capture_output=True, text=True, timeout=LIST_TIMEOUT, check=True,
            )
            by_name: dict[str, tuple[int, int]] = {}
            by_fdid: dict[int, int] = {}
            reader = csv.DictReader(p.stdout.splitlines())
            for row in reader:
                # Entries with no FileDataID at all (CASC_INVALID_ID, e.g.
                # loose CDN-content-hash-named blobs -- see this project's
                # FAILURES.md item on --unresolved-only) have a blank fdid
                # column; not nameable, not a real corpus file to compare.
                if not row["fdid"]:
                    continue
                fdid = int(row["fdid"])
                size = int(row["size"])
                by_fdid[fdid] = size
                if row["resolved"] == "1":
                    by_name[row["name"].replace("\\", "/").lower()] = (fdid, size)
            _cache = (by_name, by_fdid)
    return _cache


def _casc_size_for(rel_posix: str) -> tuple[int | None, int | None]:
    """(casc_size, fdid) for a corpus-relative path, or (None, None) if
    CASC's own listing has nothing to compare it to. Corpus-root loose
    files (no '/' at all -- `buildinfo.json`, `credits*.html`, and CASC's
    own raw manifest blobs `ENCODING`/`ROOT`/`DOWNLOAD`/`INSTALL`/`PATCH`/
    `SIZE`/`.root`, all dumped by the export tool alongside the real asset
    tree) are excluded on purpose: a real in-game asset path always has at
    least one directory component, so a bare top-level name matching
    something in the listfile is a coincidental collision, not the same
    file -- caught live: `ENCODING`/`ROOT`/etc. all "matched" some
    unrelated listfile entry by bare name and got flagged as massively
    corrupt, when they're not CASC asset content at all.
    """
    if "/" not in rel_posix:
        return None, None
    by_name, by_fdid = _load_casc_sizes()
    unresolved_name = rel_posix.rsplit("/", 1)[-1] if rel_posix.startswith("_unresolved/") else None
    if unresolved_name:
        m = UNRESOLVED_RE.match(unresolved_name)
        if not m:
            return None, None
        fdid = int(m.group(1), 16)
        return by_fdid.get(fdid), fdid
    entry = by_name.get(rel_posix.lower())
    if entry is None:
        return None, None
    fdid, size = entry
    return size, fdid


class CascSizeMismatchTask:
    GLOB_PATTERNS = ["*"]
    FIELDNAMES = ["local_size", "casc_size", "diff", "fdid", "never_extracted"]
    # Cheap in-process lookup, no subprocess per file -- see module docstring.
    PARALLEL_MODE = "thread"

    @staticmethod
    def analyze(path: Path) -> dict | None:
        if not path.is_file():
            return None
        rel_posix = path.relative_to(CORPUS_ROOT).as_posix()
        casc_size, fdid = _casc_size_for(rel_posix)
        if casc_size is None:
            return None
        local_size = path.stat().st_size
        if local_size == casc_size:
            return None
        return {
            "local_size": local_size,
            "casc_size": casc_size,
            "diff": casc_size - local_size,
            "fdid": fdid if fdid is not None else "",
            # A 0-byte local file isn't a truncated/corrupted extraction --
            # it was never written at all, not partially written and then
            # cut off. Real full-corpus run found this is NOT confined to
            # `_unresolved/`'s own empty-placeholder convention (only
            # 2,069 of 156,941 zero-byte hits live there) -- the large
            # majority are real listfile-resolved paths (e.g. several
            # `dbfilesclient/*.db2` siblings of the files this task was
            # originally written to re-check) sitting empty. Cause
            # unconfirmed as of this comment -- could be fully-encrypted-
            # no-known-key (verified for one real _unresolved sample:
            # FileDataID 7442042, 100% encrypted, casc-tool correctly
            # refuses it), could be a wholly different gap. Reported
            # separately, not folded into "truncated", specifically so a
            # reader doesn't assume this task already explains it.
            "never_extracted": local_size == 0,
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        never_extracted = [r for r in rows if r["never_extracted"]]
        truncated = [r for r in rows if not r["never_extracted"] and r["diff"] > 0]
        longer = [r for r in rows if not r["never_extracted"] and r["diff"] < 0]
        return [
            f"{len(rows)} / {total_files} corpus files have a size CASC itself disagrees with "
            f"({len(rows) / total_files:.3%} of the corpus).",
            f"  {len(truncated)} are real partial extractions (nonzero locally, short of CASC's "
            f"reported size) -- the actual re-extraction candidates, same shape as the "
            f"item.db2/itemappearance.db2/itemmodifiedappearance.db2 fix.",
            f"  {len(never_extracted)} were never extracted at all (0 bytes locally). NOT confined "
            f"to `_unresolved/`'s own placeholder convention -- most are real listfile-resolved "
            f"paths. Cause not established by this task alone; a `casc-tool extract-batch "
            f"--from-list` worklist to investigate, not a re-extraction-of-a-broken-file one.",
            f"  {len(longer)} are LONGER locally than CASC's current build reports -- likely a "
            f"build/content mismatch between this corpus's export and the storage just scanned, "
            f"not truncation; sanity-check the build number before re-extracting these.",
        ]
