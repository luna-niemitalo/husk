#!/usr/bin/env python3
"""Generalized parallel corpus-scan framework.

Every find_*.py tool in this directory (find_texture_type_collisions.py is
the freshest example) follows the same shape: discover files under a
corpus root, run one independent per-file `analyze()` function over each,
collect the non-None results into a CSV, print/write a summary. That shape
is always safe to parallelize *across files* for the same reason
corpus_checks.py's own docstring gives for its per-file status-JSON checks:
each file's analyze() call touches only that file's own bytes and returns
a plain value -- no shared state, no cross-file write race, no lock
needed. This module is that observation turned into a reusable driver, so
a future one-off corpus tool only has to write the per-file analyze()
function (the "task assignment module") and never touch discovery, worker
management, CSV/log output, or throughput instrumentation again.

Usage as a library (preferred -- write your own thin CLI, same as every
find_*.py tool already does):

    from corpus_scan_framework import ScanTask, run_corpus_scan

    class MyTask(ScanTask):
        GLOB_PATTERNS = ["*.m2"]
        FIELDNAMES = ["texture_count"]

        @staticmethod
        def analyze(path: Path) -> dict | None:
            ...                          # return None to skip this file
            return {"texture_count": n}  # keys must match FIELDNAMES

        @staticmethod
        def summarize(rows, total_files) -> list[str]:
            return [f"{len(rows)} / {total_files} files matched"]

    run_corpus_scan(MyTask, root=Path("/media/luna/data/wow_export"),
                     output_stem="my_check")

Usage from the command line, against a task class living in its own file
(see corpus_scan_tasks/example_texture_count.py for a minimal real one):

    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.example_texture_count:TextureCountTask \\
        --root /media/luna/data/wow_export --output-stem texture_count

Parallelism model: ProcessPoolExecutor by default (real CPU parallelism,
no GIL contention, matches tasks that shell out to `husk`/`gltf_validator`
or do real per-file decoding work) -- set `PARALLEL_MODE = "thread"` on
your task class for lightweight tasks (plain struct.unpack, no
subprocesses) to skip per-file pickling/IPC overhead. Either way, workers
only ever call `analyze(path)` -- discovery, CSV writing, and summarizing
always happen in the main process, once, after every worker is done.

Concurrency is *not* a fixed number chosen up front. A first version of
this framework picked one worker count from `zpool status` topology alone
(spindle count x 4 for an all-rotational pool) and used it for the whole
run -- but that can't know whether today's file set is cold on spinning
disks or warm in the pool's L2ARC/the OS page cache, and guessing wrong
either wastes throughput (too conservative) or thrashes seeks (too
aggressive). Instead, `AdaptiveConcurrency` runs a TCP-AIMD-style live
controller during the scan itself: start near a cheap topology-based
seed, grow the in-flight task budget while measured throughput (files/
sec) keeps improving, and the instant throughput drops (the real signal
of seek thrashing or any other saturation) cut the budget by ~20% and
switch from fast probing to slow +1-per-tick growth -- the same
slow-start-then-congestion-avoidance shape TCP uses to find a link's real
capacity without knowing it in advance. This self-corrects for a warm
cache automatically: a warm run just never sees the throughput-drop
signal, so it keeps climbing toward CPU-bound territory on its own.
Per-file exceptions never abort the run: caught, recorded to
<output_stem>_errors.csv, scan continues -- a handful of unreadable/
corrupt files in a 100k+-file corpus is normal, not a reason to lose every
result gathered so far.
"""

from __future__ import annotations

import argparse
import csv
import importlib
import itertools
import os
import re
import subprocess
import sys
import time
import traceback
from concurrent.futures import FIRST_COMPLETED, Future, ProcessPoolExecutor, ThreadPoolExecutor, wait
from pathlib import Path
from typing import Protocol, runtime_checkable

try:
    from tqdm import tqdm
except ImportError:
    tqdm = None

REPO_ROOT = Path(__file__).resolve().parent.parent


@runtime_checkable
class ScanTask(Protocol):
    """The one interface a task-assignment module needs to implement.

    Subclass this (or just duck-type it -- it's a Protocol, checked
    structurally) with plain `staticmethod`s; the framework only ever
    instantiates it implicitly by calling these as functions, so a task
    module carries no per-instance state and is safe to re-import fresh in
    every worker process.
    """

    GLOB_PATTERNS: list[str]  # e.g. ["*.m2"] -- passed to root.rglob() per pattern
    FIELDNAMES: list[str]     # CSV columns for whatever analyze() returns, "path" is added automatically
    PARALLEL_MODE: str        # "process" (default) or "thread"
    BATCH_SIZE: int           # files per dispatched task, default 1 -- raise it to amortize per-task IPC overhead

    @staticmethod
    def analyze(path: Path) -> dict | None:
        """Return None to skip this file, or a dict with exactly FIELDNAMES' keys."""
        ...

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        """Return human-readable summary lines, printed and written to the .log file."""
        ...


def _default_summarize(rows: list[dict], total_files: int) -> list[str]:
    return [f"{len(rows)} / {total_files} files matched ({len(rows) / total_files:.3%} of the corpus)" if total_files else "0 files scanned"]


def _import_task(spec: str):
    """"module.path:AttrName" -> the actual attribute (a class or instance)."""
    module_path, _, attr = spec.partition(":")
    if not attr:
        raise ValueError(f"--task must be 'module.path:AttrName', got {spec!r}")
    module = importlib.import_module(module_path)
    return getattr(module, attr)


# --- worker-side state -----------------------------------------------------
# Re-imported per process (ProcessPoolExecutor initializer), not pickled --
# keeps task modules free to hold non-picklable globals (compiled regexes,
# open handles, whatever) without the framework caring.
_worker_task = None


def _init_worker(task_spec: str) -> None:
    global _worker_task
    _worker_task = _import_task(task_spec)


def _analyze_one(path_str: str) -> tuple[str, dict | None, str | None]:
    try:
        result = _worker_task.analyze(Path(path_str))
    except Exception:
        return path_str, None, traceback.format_exc()
    return path_str, result, None


def _worker_analyze_batch(path_strs: list[str]) -> list[tuple[str, dict | None, str | None]]:
    """One future's worth of work. A batch of >1 amortizes ProcessPoolExecutor's
    per-task pickle/IPC round trip (submit the path, return the result dict)
    across many files instead of paying it once per file -- set
    ScanTask.BATCH_SIZE > 1 when analyze() itself is cheap enough that this
    dispatch overhead, not file I/O or CPU parsing, is the bottleneck (see
    the files/sec comparison in corpus_scan_tasks' own benchmark tasks).
    Concurrency (how many files are being read at once) is controlled by
    AdaptiveConcurrency's window (in-flight *batches*), not by this -- a
    bigger batch does not mean more files open at once, just fewer trips
    across the process boundary.
    """
    return [_analyze_one(p) for p in path_strs]


# --- discovery ---------------------------------------------------------

def discover(root: Path, patterns: list[str], limit: int | None = None) -> list[Path]:
    """Full, sorted corpus listing when `limit` is None -- needed for a
    real scan's own total_files/percentage stats to mean anything.

    With `limit` set (--limit is documented as a testing/benchmarking
    knob), short-circuits the underlying rglob generators as soon as
    enough matches are found instead of walking and sorting the entire
    ~130k-file corpus first and throwing away everything past `limit` --
    a real perf bug found via this framework's own benchmark script
    unexpectedly making a --limit run look *slower* than a plain
    single-threaded script that never pays a corpus-wide walk at all
    (tools/benchmark_texture_type_collisions.py). Order isn't guaranteed
    to match the unlimited case (filesystem walk order, not lexicographic)
    -- fine for its documented "testing" purpose, not used for real runs.
    """
    if limit is None:
        seen: set[Path] = set()
        for pattern in patterns:
            seen.update(root.rglob(pattern))
        return sorted(seen)

    found: list[Path] = []
    seen: set[Path] = set()
    for pattern in patterns:
        for p in root.rglob(pattern):
            if p in seen:
                continue
            seen.add(p)
            found.append(p)
            if len(found) >= limit:
                return sorted(found)
    return sorted(found)


# --- adaptive concurrency (TCP-AIMD-style) --------------------------------

class AdaptiveConcurrency:
    """Live in-flight-task-budget controller, fed one throughput sample
    (files/sec) per tick via record().

    Reacts only to measured throughput -- never to storage topology,
    cache state, or any other prediction -- so it converges to the real
    sweet spot whether the current file set is cold on spinning disks or
    warm in cache, without needing to know which. Mirrors TCP's own
    congestion control: "probe" mode grows faster (find the neighborhood
    quickly) until the first throughput drop, then permanently switches
    to "linear" mode (grow by +1 per good tick -- slow re-probing, so a
    later improvement, e.g. the cache warming up mid-run, still gets
    found) with a multiplicative cut (20% by default) on every drop.
    Growth is capped at +max_growth_step per tick even in "probe" mode
    (default 4, not a doubling) -- a real benchmark this session showed a
    thread-pool run overshoot badly on a GIL-bound task specifically
    because a halving-sized jump (e.g. 28 -> 42) blew straight past the
    sweet spot before the next tick could measure the damage; a small
    fixed cap keeps every single overshoot cheap enough that the
    backoff on the very next tick is a minor correction, not a recovery.
    The default backoff_threshold (25%, not TCP's typical single-lost-
    packet trigger) exists for the same reason min_samples_per_tick does
    on the caller side: a real corpus's per-file cost can be extremely
    heavy-tailed (this project's own .m2 files range from a few bytes to
    tens of MB, and this framework's own texture-collision task also
    loops over a variable, sometimes-huge number of .skin batches per
    file) -- at a small window, one slow file landing in a single tick is
    enough to look like a real throughput drop under a tight threshold,
    triggering a spurious cut. A quarter-drop margin absorbs ordinary
    single-outlier noise from that variance and still catches genuine
    thrashing (which drags the *average* down, not just one sample).
    """

    def __init__(self, initial_window: int, max_window: int, min_window: int = 1,
                 backoff_threshold: float = 0.25, backoff_factor: float = 0.8, max_growth_step: int = 4):
        self.window = max(min_window, min(initial_window, max_window))
        self.min_window = min_window
        self.max_window = max_window
        self.backoff_threshold = backoff_threshold  # throughput drop, relative, that counts as "thrashing"
        self.backoff_factor = backoff_factor         # multiplicative cut applied on that signal
        self.max_growth_step = max_growth_step       # hard cap on window increase per tick, any mode
        self.mode = "probe"
        self.backoff_count = 0
        self.history: list[tuple[float, int, float]] = [(0.0, self.window, 0.0)]  # (elapsed_s, window, rate)
        self._prev_rate: float | None = None
        self._cooldown = False  # skip one growth tick right after a cut, let the new window stabilize

    def record(self, elapsed_s: float, rate: float) -> int:
        if self._prev_rate is not None and self._prev_rate > 0 and rate < self._prev_rate * (1 - self.backoff_threshold):
            self.window = max(self.min_window, round(self.window * self.backoff_factor))
            self.mode = "linear"
            self.backoff_count += 1
            self._cooldown = True
        elif self._cooldown:
            self._cooldown = False  # held flat this tick; resume growth next tick
        else:
            step = min(self.max_growth_step, max(1, self.window // 2)) if self.mode == "probe" else 1
            self.window = min(self.max_window, self.window + step)
        self._prev_rate = rate
        self.history.append((elapsed_s, self.window, rate))
        return self.window

    def trace_str(self) -> str:
        return "→".join(str(w) for _, w, _ in self.history)


# --- storage introspection (best-effort, never fatal) -----------------
# Used only to seed AdaptiveConcurrency's starting window -- a ramp-up
# time optimization, not a cap. If any of this fails or the corpus root
# isn't on a recognizable ZFS pool, the controller starts from a small
# fixed seed and probes upward on its own; either way it converges to the
# same place, just slower without a good seed.

def _leaf_devices_for_pool(pool: str) -> list[str] | None:
    """Parses the `config:` block of `zpool status <pool>` for real leaf
    device names, excluding cache/log/spare vdevs (they don't carry the
    corpus's own data) and the free-form wrapped prose `status:`/`action:`
    lines that can precede the config block -- skipped structurally, by
    only ever looking after the `config:` line, rather than by
    pattern-matching prose text.
    """
    try:
        out = subprocess.run(["zpool", "status", pool], capture_output=True, text=True, timeout=10, check=True).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    lines = out.splitlines()
    try:
        config_idx = next(i for i, l in enumerate(lines) if l.strip() == "config:")
    except StopIteration:
        return None

    redundancy_re = re.compile(r"^(raidz|mirror|draid)\d*-\d+$")
    devices: list[str] = []
    group_indent: int | None = None
    section_excluded = False
    started = False
    for line in lines[config_idx + 1:]:
        if not line.strip():
            if started:
                break  # blank line after real content: end of the config block (e.g. before "errors:")
            continue  # blank line(s) between "config:" and the "NAME ..." header
        started = True
        if line == line.lstrip():
            break  # dedented back out of the config block
        indent = len(line) - len(line.lstrip())
        name = line.split()[0]
        if name in ("NAME", pool):
            continue
        if group_indent is None or indent <= group_indent:
            group_indent = indent
            section_excluded = name in {"cache", "logs", "spares"}
            if not section_excluded and not redundancy_re.match(name):
                devices.append(name)  # simple stripe: leaf sits directly under the pool, no group
            continue
        if not section_excluded:
            devices.append(name)
    return devices or None


def _device_is_rotational(device_id_name: str) -> bool | None:
    by_id = Path("/dev/disk/by-id") / device_id_name
    try:
        resolved = by_id.resolve(strict=True)
    except OSError:
        return None
    base = re.sub(r"p?\d+$", "", resolved.name)  # /dev/sda1 -> sda, /dev/nvme0n1p1 -> nvme0n1
    rota_path = Path("/sys/block") / base / "queue" / "rotational"
    try:
        return rota_path.read_text().strip() == "1"
    except OSError:
        return None


def _pool_for_path(root: Path) -> str | None:
    try:
        dataset = subprocess.run(
            ["df", "--output=source", str(root)], capture_output=True, text=True, timeout=10, check=True
        ).stdout.strip().splitlines()[-1]
        return dataset.split("/")[0]
    except (OSError, subprocess.SubprocessError, IndexError):
        return None


def _initial_window_seed(root: Path, max_window: int) -> tuple[int, str]:
    """Best-effort initial AdaptiveConcurrency window + a one-line
    explanation. Never raises -- falls back to a small fixed seed.
    """
    fallback = min(max_window, 4)
    pool = _pool_for_path(root)
    if pool is None:
        return fallback, f"{root} not on a recognizable ZFS pool -- starting from a fixed seed ({fallback})"
    devices = _leaf_devices_for_pool(pool)
    if not devices:
        return fallback, f"pool {pool!r} leaf devices undetermined -- starting from a fixed seed ({fallback})"
    rotational_flags = [_device_is_rotational(d) for d in devices]
    if any(flag is None for flag in rotational_flags) or not any(rotational_flags):
        return fallback, f"pool {pool!r} non-rotational or undetermined -- starting from a fixed seed ({fallback})"
    spindles = len(devices)
    seed = max(1, min(max_window, spindles * 2))
    return seed, f"pool {pool!r} has {spindles} rotational leaf device(s) -- seeding at {spindles}x2={seed}, controller will probe from there"


def _read_diskstats(devices: list[str]) -> dict[str, int]:
    """device basename ('sda') -> cumulative sectors read, from /proc/diskstats."""
    out: dict[str, int] = {}
    try:
        for line in Path("/proc/diskstats").read_text().splitlines():
            fields = line.split()
            name = fields[2]
            if name in devices:
                out[name] = int(fields[5])  # field 6: sectors read successfully
    except OSError:
        pass
    return out


def _resolve_leaf_basenames(root: Path) -> list[str] | None:
    pool = _pool_for_path(root)
    if pool is None:
        return None
    devices = _leaf_devices_for_pool(pool)
    if not devices:
        return None
    try:
        return [re.sub(r"p?\d+$", "", (Path("/dev/disk/by-id") / d).resolve(strict=True).name) for d in devices]
    except OSError:
        return None


_ARCSTATS_PATH = Path("/proc/spl/kstat/zfs/arcstats")
_ARCSTATS_FIELDS = ("hits", "misses", "l2_hits", "l2_misses")


def _read_arcstats() -> dict[str, int]:
    """Wanted fields from /proc/spl/kstat/zfs/arcstats ("name type value"
    per line). Returns {} on any read failure (non-ZFS system, no L2ARC,
    permissions) -- purely diagnostic, never required for a decision.
    """
    out: dict[str, int] = {}
    try:
        for line in _ARCSTATS_PATH.read_text().splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[0] in _ARCSTATS_FIELDS:
                out[parts[0]] = int(parts[2])
    except OSError:
        pass
    return out


# --- driver --------------------------------------------------------------

def run_corpus_scan(
    task,
    root: Path,
    output_stem: str,
    initial_workers: int | None = None,
    max_workers: int | None = None,
    limit: int | None = None,
    output_dir: Path | None = None,
    tick_seconds: float = 6.0,
    min_samples_per_tick: int = 15,
    batch_size: int | None = None,
) -> int:
    """Runs `task` over every file under `root` matching task.GLOB_PATTERNS,
    with a live-adjusted concurrency budget (see AdaptiveConcurrency), and
    writes <output_stem>.csv / <output_stem>_scan.log / <output_stem>_errors.csv
    (only if any errors occurred) to output_dir (default: repo root,
    matching every existing find_*.py convention). Returns the process
    exit code (0 on success).
    """
    output_dir = output_dir or REPO_ROOT
    fieldnames = ["path", *task.FIELDNAMES]
    parallel_mode = getattr(task, "PARALLEL_MODE", "process")
    summarize = getattr(task, "summarize", _default_summarize)
    batch_size = batch_size if batch_size is not None else getattr(task, "BATCH_SIZE", 1)

    print(f"scanning {root} for {task.GLOB_PATTERNS}")
    files = discover(root, task.GLOB_PATTERNS, limit=limit)
    total_files = len(files)
    print(f"found {total_files} files")
    if total_files == 0:
        return 0

    if max_workers is None:
        max_workers = os.cpu_count() or 4
    if initial_workers is None:
        initial_workers, seed_reason = _initial_window_seed(root, max_workers)
        print(f"initial concurrency window: {initial_workers} ({seed_reason})")
    controller = AdaptiveConcurrency(initial_window=initial_workers, max_window=max_workers)

    task_spec = f"{task.__module__}:{task.__qualname__}"
    leaf_devices = _resolve_leaf_basenames(root)
    disk_before = _read_diskstats(leaf_devices) if leaf_devices else {}
    arc_before = _read_arcstats()

    rows: list[dict] = []
    errors: list[tuple[str, str]] = []
    start = time.monotonic()
    scanned = 0

    executor_cls = ThreadPoolExecutor if parallel_mode == "thread" else ProcessPoolExecutor
    executor_kwargs = {} if parallel_mode == "thread" else {"initializer": _init_worker, "initargs": (task_spec,)}
    if parallel_mode == "thread":
        _init_worker(task_spec)  # populate _worker_task in-process; threads share it

    progress = tqdm(total=total_files, desc=f"scanning ({parallel_mode}, window={controller.window}, batch={batch_size})", unit="file") if tqdm else None
    with executor_cls(max_workers=max_workers, **executor_kwargs) as pool:
        pending = iter(files)
        in_flight: set[Future] = set()

        def top_up() -> None:
            while len(in_flight) < controller.window:
                chunk = [str(p) for p in itertools.islice(pending, batch_size)]
                if not chunk:
                    return
                in_flight.add(pool.submit(_worker_analyze_batch, chunk))

        top_up()
        tick_start = start
        tick_completed = 0
        while in_flight:
            done, _pending_futures = wait(in_flight, timeout=0.5, return_when=FIRST_COMPLETED)
            for future in done:
                in_flight.discard(future)
                batch_results = future.result()
                for path_str, result, error in batch_results:
                    scanned += 1
                    tick_completed += 1
                    if error is not None:
                        errors.append((path_str, error))
                    elif result is not None:
                        rows.append({"path": path_str, **result})
                if progress:
                    progress.update(len(batch_results))

            now = time.monotonic()
            if now - tick_start >= tick_seconds and tick_completed >= min_samples_per_tick:
                rate = tick_completed / (now - tick_start)
                new_window = controller.record(now - start, rate)
                if progress:
                    progress.set_description(f"scanning ({parallel_mode}, window={new_window})")
                else:
                    print(f"  scanned {scanned}/{total_files} window={new_window} rate={rate:.0f} files/sec")
                tick_start = now
                tick_completed = 0
            top_up()
    if progress:
        progress.close()

    elapsed = time.monotonic() - start
    disk_after = _read_diskstats(leaf_devices) if leaf_devices else {}
    arc_after = _read_arcstats()
    print(f"\nscanned {total_files} files in {elapsed:.1f}s ({total_files / elapsed:.0f} files/sec avg, "
          f"{parallel_mode} pool, max {max_workers} workers)")
    print(f"  concurrency window trace: {controller.trace_str()} ({controller.backoff_count} backoff(s), "
          f"converged at {controller.window})")
    if disk_before and disk_after:
        for dev in sorted(disk_before):
            sectors = disk_after.get(dev, disk_before[dev]) - disk_before[dev]
            mb = sectors * 512 / 1_048_576
            print(f"  {dev}: {mb:.1f} MB read ({mb / elapsed:.1f} MB/s)")
    if arc_before and arc_after:
        arc_hits = arc_after["hits"] - arc_before["hits"]
        arc_misses = arc_after["misses"] - arc_before["misses"]
        if arc_hits + arc_misses > 0:
            print(f"  ARC hit rate: {arc_hits / (arc_hits + arc_misses):.1%} ({arc_hits} hits / {arc_misses} misses)")
        l2_hits = arc_after["l2_hits"] - arc_before["l2_hits"]
        l2_misses = arc_after["l2_misses"] - arc_before["l2_misses"]
        if l2_hits + l2_misses > 0:
            print(f"  L2ARC hit rate: {l2_hits / (l2_hits + l2_misses):.1%} ({l2_hits} hits / {l2_misses} misses)")

    csv_path = output_dir / f"{output_stem}.csv"
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"written to {csv_path}")

    if errors:
        errors_path = output_dir / f"{output_stem}_errors.csv"
        with errors_path.open("w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=["path", "error"])
            writer.writeheader()
            for path_str, error in errors:
                writer.writerow({"path": path_str, "error": error})
        print(f"{len(errors)} per-file errors written to {errors_path}")

    summary_lines = ["", *summarize(rows, total_files), f"written to {csv_path}"]
    log_path = output_dir / f"{output_stem}_scan.log"
    for line in summary_lines:
        print(line)
    log_path.write_text("\n".join(summary_lines) + "\n")
    print(f"summary also written to {log_path}")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--task", required=True, help="'module.path:AttrName' of a ScanTask-shaped class")
    parser.add_argument("--root", type=Path, default=Path("/media/luna/data/wow_export"))
    parser.add_argument("--output-stem", required=True)
    parser.add_argument("--max-workers", type=int, default=None, help="concurrency ceiling, default: CPU count")
    parser.add_argument("--initial-workers", type=int, default=None,
                         help="starting concurrency window, default: auto-seeded from backing storage topology")
    parser.add_argument("--limit", type=int, default=None, help="only scan the first N discovered files (testing)")
    parser.add_argument("--output-dir", type=Path, default=None, help="default: repo root")
    parser.add_argument("--batch-size", type=int, default=None,
                         help="files per dispatched task, default: task.BATCH_SIZE or 1")
    args = parser.parse_args()

    sys.path.insert(0, str(REPO_ROOT / "tools"))
    task = _import_task(args.task)
    return run_corpus_scan(task, root=args.root, output_stem=args.output_stem, max_workers=args.max_workers,
                            initial_workers=args.initial_workers, limit=args.limit, output_dir=args.output_dir,
                            batch_size=args.batch_size)


if __name__ == "__main__":
    sys.exit(main())
