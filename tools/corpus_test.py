#!/usr/bin/env python3
"""Corpus-wide smoke/regression runner: runs the compiled `husk` binary
against every real .m2 file found under a WoW extraction root (Luna's own
personally-owned extraction, never CASC -- same convention as test_data/'s
fixtures), reporting live pass/fail coverage instead of the handful of
curated fixtures tests/test_integration.cpp exercises.

Not a replacement for that doctest suite: doctest's real-data tests assert
exact, model-specific values (vertex counts, emitter counts, ...) -- that
only makes sense against fixtures whose expected values were hand-derived
once, per test_data_paths.hpp's own doc comments. This tool asks a
different, generic question that scales to arbitrary files: does this file
produce a plausible, internally-consistent, full-pipeline-correct output,
without needing a pre-known "right answer" for each of ~130k files. Per
file:

  - header: not just "did husk info exit 0" -- classifies *why* not, via
    husk's own descriptive ParseError/ChunkError text (truncated file, bad
    magic, chunk-container corruption, array-bounds violation, ...), and
    cross-checks against an independent from-scratch Python header read
    (not calling husk at all) as a genuine second opinion.
  - export: husk export writes a model bundle whose manifest.json parses
    and is a `model` document.
  - dump-chunks: (chunked files only) valid JSON output.
  - fidelity: the *independently* parsed M2 header's vertex/bone counts
    exactly match the bundle's mesh vertex count and skeleton joint count
    -- same exact-match bar tests/test_conformance.cpp's fixture checks
    use, generalized to work without a hardcoded expected value.
  - finite: every f32 array the manifest names (positions, normals, UVs,
    every LOD level, skeleton, *and* every animation keyframe) is swept for
    NaN/Inf -- a static proxy for "doesn't explode when animated": a
    keyframe that's already non-finite at rest will definitely misbehave
    the moment anything interpolates it.
  - mesh_completeness: the mesh and every LOD level carry UV set 0 for
    every vertex, and every primitive names a material the bundle has.
  - validate: zero `husk validate-bundle` problems -- schema-level bundle
    correctness is *not* reimplemented here; the embedded schemas are the
    format's definition.

The bundle reader and the export/content/validate checks live in
corpus_checks.py (one implementation; this runner adds scheduling and
reporting).

Dependencies (tqdm, rich) live in tools/venv, a uv-managed venv -- not
this project's usual bare-stdlib scratch-script convention, deliberately:
a hand-rolled progress bar/ANSI-clearing dance is exactly the kind of
"don't reinvent it" case CLAUDE.md's readability rules call out, so this
uses the same idiomatic libraries anyone else reaching for a concurrent
progress-bar CLI would. Set up once via:
    cd tools && uv venv venv && uv pip install --python venv/bin/python tqdm rich

Usage (direnv -> uv run -> the venv's python -> this script):
    direnv exec . uv run --python tools/venv/bin/python tools/corpus_test.py [options]
    direnv exec . uv run --python tools/venv/bin/python tools/corpus_test.py --limit 200 --verbose
    direnv exec . uv run --python tools/venv/bin/python tools/corpus_test.py --sample 5000 --report /tmp/report.json

Why validation is batched AND pooled, not one-per-file: each `husk
validate-bundle` invocation pays process startup and the embedded schema
load once (about half a second) and takes any number of bundle
directories, so one invocation per batch amortizes that. husk's own info/
export/dump-chunks run on a large thread pool; validation runs on a small
pool of persistent worker threads (--validator-workers), each pulling one
already-exported batch at a time off a *bounded* queue.Queue so the export
producer can't race arbitrarily far ahead of validation and pile up
unbounded bundle data in tmpfs.
"""

import argparse
import concurrent.futures
import json
import os
import queue
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

import corpus_checks as cc
from corpus_checks import AssertResult, TestResult

try:
    from tqdm import tqdm
    from rich.console import Console
except ImportError:
    sys.exit(
        "error: tqdm/rich not found. Set up the venv once:\n"
        "  cd tools && uv venv venv && uv pip install --python venv/bin/python tqdm rich\n"
        "then run via:\n"
        "  direnv exec . uv run --python tools/venv/bin/python tools/corpus_test.py ..."
    )

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_HUSK_BIN = REPO_ROOT / "build" / "husk"
DEFAULT_CORPUS_ROOT = Path("/media/luna/wow_export/live")


@dataclass
class FileResult:
    path: str
    tests: list = field(default_factory=list)  # list[TestResult]

    @property
    def test_pass(self) -> int:
        return sum(1 for t in self.tests if t.passed)

    @property
    def test_fail(self) -> int:
        return sum(1 for t in self.tests if not t.passed)

    @property
    def assert_pass(self) -> int:
        return sum(1 for t in self.tests for a in t.asserts if a.passed)

    @property
    def assert_fail(self) -> int:
        return sum(1 for t in self.tests for a in t.asserts if not a.passed)

    def failure_codes(self):
        return [f"{t.name}/{a.code or a.name}" for t in self.tests for a in t.asserts if not a.passed]


def run_cmd(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except subprocess.TimeoutExpired:
        return -1, f"TIMEOUT after {timeout}s: {' '.join(cmd)}"
    except OSError as e:
        return -2, f"OSError launching {cmd[0]}: {e}"


def last_meaningful_line(out: str) -> str:
    # husk always prints its actual error/failure message as the final
    # line before a nonzero exit (see main.cpp's top-level catch) -- any
    # "husk: note: ..." lines print earlier, during a successful parse
    # stage that ran before the failure.
    for line in reversed(out.splitlines()):
        if line.strip():
            return line.strip()[:400]
    return ""


# Same fixed field offsets husk's own src/m2.cpp uses (verified against
# real data earlier this session -- see WIKI_FINDINGS.md) -- reimplemented
# from scratch here, not called into husk, so this is a genuinely
# independent second opinion on "does this look like a real M2," not husk
# checking its own homework.
_M2_VERSION_OFFSET = 0x04
_M2_BONES_COUNT_OFFSET = 0x02C
_M2_VERTICES_COUNT_OFFSET = 0x03C
_M2_MIN_HEADER_SIZE = 0x090


def read_m2_header_summary(m2_path: Path):
    """Independent (no husk involved) minimal M2 header read: magic/chunk
    container walk + a handful of fixed-offset fields. Returns None if the
    file isn't even recognizable as MD20/MD21-wrapped -- the `header` test
    reports that as its own distinct outcome, not folded into "husk failed
    for some reason."
    """
    data = m2_path.read_bytes()
    if len(data) < 8:
        return None
    if data[0:4] == b"MD20":
        blob = data
    else:
        pos = 0
        blob = None
        while pos + 8 <= len(data):
            ctag = data[pos:pos + 4]
            csize = struct.unpack_from("<I", data, pos + 4)[0]
            payload_start = pos + 8
            if payload_start + csize > len(data):
                break  # truncated chunk container
            if ctag == b"MD21":
                blob = data[payload_start:payload_start + csize]
                break
            pos = payload_start + csize
        if blob is None:
            return None
    if len(blob) < _M2_MIN_HEADER_SIZE or blob[0:4] != b"MD20":
        return None
    version = struct.unpack_from("<I", blob, _M2_VERSION_OFFSET)[0]
    bone_count = struct.unpack_from("<I", blob, _M2_BONES_COUNT_OFFSET)[0]
    vertex_count = struct.unpack_from("<I", blob, _M2_VERTICES_COUNT_OFFSET)[0]
    return {"version": version, "bone_count": bone_count, "vertex_count": vertex_count}


# Buckets husk's own descriptive ParseError/ChunkError text (see src/m2.cpp,
# src/chunk.cpp) into categories that make sense to group across an entire
# corpus -- "N files failed because X", not one distinct message per file.
# Order matters: first match wins, most-specific patterns first.
_FAILURE_CATEGORIES = [
    ("TRUNCATED_FILE", ("too short to even contain a magic value",)),
    ("NOT_RECOGNIZED_AS_M2", ("doesn't start with md20", "no md21 chunk")),
    ("CHUNK_CONTAINER_TRUNCATED", ("truncated chunk header", "declares size")),
    ("ARRAY_BOUNDS_VIOLATION", ("out of range", "needs more room", "claims")),
    ("SKIN_MISMATCH", ("skin index", "out of range for the skin")),
]


def classify_parse_failure(message: str) -> str:
    lower = message.lower()
    for category, needles in _FAILURE_CATEGORIES:
        if any(n in lower for n in needles):
            return category
    return "OTHER_PARSE_ERROR"


def test_header(husk_bin, m2_path, timeout):
    """Not just "did husk info exit 0" -- classifies *why* when it doesn't,
    and cross-checks against an independent (non-husk) parse of the same
    file, per the corpus tool's actual purpose: "does this look like a
    functional file, or is it borked, and as a result of what."
    """
    code, out = run_cmd([str(husk_bin), "info", str(m2_path)], timeout)
    independent = read_m2_header_summary(m2_path)
    asserts = []

    if code == 0:
        asserts.append(AssertResult("recognized", True))
        asserts.append(AssertResult("has_version_line", "version:" in out,
                                     detail="" if "version:" in out else out[:400]))
        # husk parsed it; does an independent from-scratch reader see
        # something in the same ballpark? A mismatch here means either
        # husk is more lenient/lax than this minimal reader (expected for
        # some legitimately-tricky-but-valid files) or there's a genuine
        # disagreement worth a human look -- flagged, not treated as fatal.
        if independent is not None:
            asserts.append(AssertResult(
                "independent_vertex_count_plausible", 0 <= independent["vertex_count"] <= 10_000_000,
                detail="" if 0 <= independent["vertex_count"] <= 10_000_000
                else f"independent parse got vertex_count={independent['vertex_count']}"))
    else:
        reason = last_meaningful_line(out)
        category = classify_parse_failure(reason)
        asserts.append(AssertResult("recognized", False, code=category, detail=reason))

    return TestResult("header", asserts), "Legion+ chunked" in out, independent


def test_export(m2_path, bundle_dir, anim_mode):
    args = ["--textures", "none", "--anim", anim_mode, "--bones-dir", "none"]
    return cc._export_result(m2_path, bundle_dir, args), (bundle_dir / "manifest.json").exists()


def test_dump_chunks(husk_bin, m2_path, timeout):
    code, out = run_cmd([str(husk_bin), "dump-chunks", str(m2_path)], timeout)
    looks_json = out.strip().startswith("{")
    asserts = [
        AssertResult("exit_code_0", code == 0, detail=last_meaningful_line(out) if code != 0 else ""),
        AssertResult("looks_like_json", looks_json, detail="" if looks_json else out[:200]),
    ]
    return TestResult("dump-chunks", asserts)


def test_bundle_content(bundle_dir: Path, header_summary):
    """Runs fidelity/finite/mesh_completeness against one manifest read (a
    character manifest is over 100 MB of JSON). A manifest this reader
    can't open reports that failure once instead of silently skipping
    every content check.
    """
    try:
        manifest = cc.read_manifest(bundle_dir)
    except (OSError, json.JSONDecodeError) as e:
        return [TestResult("bundle_content", [AssertResult("manifest_parseable", False, detail=str(e))])]
    results = [
        cc.fidelity_result(manifest, header_summary),
        cc.finite_result(bundle_dir, manifest),
        cc.mesh_completeness_result(manifest),
    ]
    return [r for r in results if r is not None]


_name_counter = 0
_name_counter_lock = threading.Lock()


def unique_bundle_path(batch_dir, m2_path):
    global _name_counter
    with _name_counter_lock:
        _name_counter += 1
        n = _name_counter
    return batch_dir / f"{n}_{Path(m2_path).stem}.bundle"


def process_file_phase1(m2_path, batch_dir, husk_bin, opts):
    """Runs every check that's fast/native and scales cleanly with a large
    thread pool: husk info/export/dump-chunks, plus the bundle content
    checks (fidelity/finite/mesh_completeness -- pure Python, no
    subprocess, so they belong here, not on the small validator worker
    pool). Does NOT run the validator itself -- that's batched separately
    (see module docstring) -- and does NOT delete the bundle: the caller
    needs it around for the batch-wide validator pass, and cleans up the
    whole batch directory afterward instead.
    """
    result = FileResult(path=str(m2_path))
    bundle_dir = unique_bundle_path(batch_dir, m2_path)

    header_result, is_chunked, header_summary = test_header(husk_bin, m2_path, opts.timeout)
    result.tests.append(header_result)

    export_result, bundle_exists = test_export(m2_path, bundle_dir, opts.anim)
    result.tests.append(export_result)

    if is_chunked and not opts.skip_dump:
        result.tests.append(test_dump_chunks(husk_bin, m2_path, opts.timeout))

    if bundle_exists and not opts.skip_content_checks:
        result.tests.extend(test_bundle_content(bundle_dir, header_summary))

    return result, (bundle_dir if bundle_exists else None)


def validate_and_report_batch(batch_dir, file_results, bundle_to_result,
                               opts, reporter, results, results_lock):
    """Owns one batch's validation, reporting, and cleanup end to end -- the
    unit of work a validator worker pulls off the bounded queue (see
    validator_worker_loop/main()). Runs exactly one `husk validate-bundle`
    invocation for the whole batch (amortizing its startup across every
    file in it, see module docstring), merges each bundle's own report
    back into its FileResult, then reports and records every
    file in this batch itself: this worker owns this batch's output start
    to finish, so multiple validator workers running concurrently never
    contend over a single central "merge results" step -- only the shared
    `results` list append is (briefly) locked, and the printed lines/
    progress bar go through tqdm/rich, both already safe for concurrent use
    from worker threads.
    """
    try:
        if not opts.skip_validator and bundle_to_result:
            cc.run_bundle_validator(list(bundle_to_result), opts.validator_timeout)
            for bundle_dir, result in bundle_to_result.items():
                report = json.loads(cc.validator_report_path(bundle_dir.resolve()).read_text())
                result.tests.append(cc.validate_result_from_report(report))
        with results_lock:
            results.extend(file_results.values())
        for result in file_results.values():
            reporter.report(result, opts.root)
    finally:
        shutil.rmtree(batch_dir, ignore_errors=True)


def validator_worker_loop(work_queue, opts, reporter, results, results_lock):
    """Runs forever in its own thread, pulling one already-exported batch at
    a time off `work_queue` -- one persistent 'parser' per worker, matching
    export's own per-file worker shape but at batch granularity, since the
    validator's fixed per-invocation cost (see module docstring) needs
    many files per invocation to amortize well. `work_queue` is a *bounded*
    queue.Queue (see main()): its own put()/get() blocking is what keeps the
    export producer from racing arbitrarily far ahead and piling up
    unbounded bundle data in tmpfs -- a plain generator can lazily produce
    batches, but only a bounded queue between threads actually throttles
    the producer against how fast these workers are draining it.
    """
    while True:
        item = work_queue.get()
        try:
            if item is None:  # sentinel: no more batches, this worker is done
                return
            batch_dir, file_results, bundle_to_result = item
            validate_and_report_batch(batch_dir, file_results, bundle_to_result,
                                       opts, reporter, results, results_lock)
        finally:
            work_queue.task_done()


def discover_files(root: Path, pattern: str, limit, sample, seed):
    files = sorted(root.rglob(pattern))
    if sample is not None:
        rng = random.Random(seed)
        files = rng.sample(files, min(sample, len(files)))
        files.sort()
    if limit is not None:
        files = files[:limit]
    return files


def chunked(seq, size):
    for i in range(0, len(seq), size):
        yield seq[i:i + size]


class Reporter:
    """Colors per-file result lines via rich, prints them through tqdm.write()
    so they never clobber the live progress bar (tqdm's own supported
    pattern for this -- see its docs on logging alongside a bar), instead
    of hand-rolling ANSI clear-line/redraw bookkeeping.
    """

    def __init__(self, total, use_color, quiet, only_failures):
        self.pbar = tqdm(total=total, unit="file", dynamic_ncols=True,
                          bar_format="{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}, {rate_fmt}]")
        self.console = Console(force_terminal=use_color, no_color=not use_color, highlight=False)
        self.quiet = quiet
        self.only_failures = only_failures
        self.file_fail_count = 0

    def _styled(self, text, style) -> str:
        with self.console.capture() as cap:
            self.console.print(text, style=style, end="")
        return cap.get()

    def report(self, result: FileResult, root: Path):
        has_failure = result.test_fail > 0
        if has_failure:
            self.file_fail_count += 1
        if not self.quiet and (has_failure or not self.only_failures):
            try:
                name = str(Path(result.path).relative_to(root))
            except ValueError:
                name = result.path
            line = (
                f"{name} Tests {self._styled(result.test_pass, 'green')}"
                f"|{self._styled(result.test_fail, 'red')}"
                f": Asserts {self._styled(result.assert_pass, 'green')}"
                f"|{self._styled(result.assert_fail, 'red')}"
            )
            if has_failure:
                line += f"  {self._styled(', '.join(result.failure_codes()), 'dim')}"
            tqdm.write(line)
        self.pbar.update(1)

    def close(self):
        self.pbar.close()


def print_summary(results, elapsed, console):
    total = len(results)
    files_all_pass = sum(1 for r in results if r.test_fail == 0)
    files_any_fail = total - files_all_pass
    test_pass = sum(r.test_pass for r in results)
    test_fail = sum(r.test_fail for r in results)
    assert_pass = sum(r.assert_pass for r in results)
    assert_fail = sum(r.assert_fail for r in results)

    console.rule("[bold]Corpus test summary")
    console.print(f"Files:   {total} total, "
                   f"[green]{files_all_pass} clean[/green] / [red]{files_any_fail} with failures[/red]")
    console.print(f"Tests:   [green]{test_pass} passed[/green] / [red]{test_fail} failed[/red]")
    console.print(f"Asserts: [green]{assert_pass} passed[/green] / [red]{assert_fail} failed[/red]")
    console.print(f"Elapsed: {elapsed:.1f}s ({total / elapsed if elapsed else 0:.1f} files/s)")

    reason_counter = Counter()
    example_for_reason = {}
    detail_for_reason = {}
    for r in results:
        for t in r.tests:
            for a in t.asserts:
                if a.passed:
                    continue
                key = f"{t.name}/{a.code or a.name}"
                reason_counter[key] += 1
                example_for_reason.setdefault(key, r.path)
                detail_for_reason.setdefault(key, a.detail)

    if reason_counter:
        console.print(f"\n[bold]Top failure commonalities (of {len(reason_counter)} distinct):[/bold]")
        for reason, count in reason_counter.most_common(20):
            console.print(f"  [red]{count:6d}[/red]  {reason}  [dim]{detail_for_reason[reason][:100]}[/dim]")
            console.print(f"          [dim]e.g. {example_for_reason[reason]}[/dim]")
    console.rule()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=DEFAULT_CORPUS_ROOT, help="corpus root to scan for .m2 files")
    ap.add_argument("--husk-bin", type=Path, default=DEFAULT_HUSK_BIN, help="path to the compiled husk binary")
    ap.add_argument("--pattern", default="*.m2", help="glob pattern under --root (default: *.m2)")
    ap.add_argument("--workers", type=int, default=os.cpu_count() or 4, help="parallel worker count for husk calls")
    # Small: one validate-bundle startup (~0.5 s) is already negligible per
    # file at 50, and every bundle of a batch sits in tmpfs until the batch
    # is validated -- a character bundle is ~340 MB.
    ap.add_argument("--batch-size", type=int, default=50,
                     help="files per `husk validate-bundle` batch (one startup per batch, not per file)")
    ap.add_argument("--validator-workers", type=int, default=4,
                     help="concurrent `husk validate-bundle` processes (default: 4)")
    ap.add_argument("--limit", type=int, default=None, help="only process the first N discovered files")
    ap.add_argument("--sample", type=int, default=None, help="randomly sample N files instead of all")
    ap.add_argument("--seed", type=int, default=0, help="RNG seed for --sample (reproducible by default)")
    ap.add_argument("--anim", default="auto", choices=["auto", "inline", "none"],
                     help="--anim mode passed to husk export (default: auto, most thorough; "
                          "use 'inline' for a faster pass that skips external .anim directory search)")
    ap.add_argument("--timeout", type=float, default=30.0, help="per-husk-invocation timeout, seconds")
    ap.add_argument("--validator-timeout", type=float, default=120.0,
                     help="per-batch `husk validate-bundle` timeout, seconds")
    ap.add_argument("--skip-validator", action="store_true", help="skip the validate-bundle pass entirely")
    ap.add_argument("--skip-dump", action="store_true", help="skip the dump-chunks pass on chunked files")
    ap.add_argument("--skip-content-checks", action="store_true",
                     help="skip fidelity/finite/mesh_completeness bundle content checks")
    ap.add_argument("--ram-dir", type=Path, default=Path("/dev/shm"),
                     help="tmpfs root for scratch bundle output (default: /dev/shm)")
    ap.add_argument("--quiet", action="store_true", help="progress bar only, no per-file lines")
    ap.add_argument("--only-failures", action="store_true", help="print per-file lines only for failures")
    ap.add_argument("--no-color", action="store_true", help="disable colored output")
    ap.add_argument("--report", type=Path, default=None, help="write a full JSON report to this path")
    opts = ap.parse_args()

    if not opts.husk_bin.exists():
        print(f"error: husk binary not found at {opts.husk_bin} -- build it first (cmake --build build)",
              file=sys.stderr)
        return 1
    if not opts.root.exists():
        print(f"error: corpus root {opts.root} does not exist", file=sys.stderr)
        return 1

    # The export and validate runs are corpus_checks.py's; its globals are
    # its one configuration point.
    cc.HUSK_BIN = str(opts.husk_bin)
    cc.TIMEOUT = opts.timeout

    use_color = (not opts.no_color) and sys.stdout.isatty()
    console = Console(force_terminal=use_color, no_color=not use_color, highlight=False)

    print(f"discovering files under {opts.root} (pattern {opts.pattern!r})...", file=sys.stderr)
    files = discover_files(opts.root, opts.pattern, opts.limit, opts.sample, opts.seed)
    total = len(files)
    if total == 0:
        print("no files matched -- nothing to do", file=sys.stderr)
        return 0
    print(f"{total} file(s) to test, {opts.workers} husk worker(s), "
          f"{opts.validator_workers} validator worker(s) x {opts.batch_size} files/batch, "
          f"scratch in tmpfs ({opts.ram_dir})",
          file=sys.stderr)

    if not opts.ram_dir.exists():
        print(f"error: --ram-dir {opts.ram_dir} does not exist (not on Linux, or no tmpfs mounted there?)",
              file=sys.stderr)
        return 1
    run_root = Path(tempfile.mkdtemp(dir=opts.ram_dir, prefix="husk-corpus-"))

    reporter = Reporter(total, use_color, opts.quiet, opts.only_failures)
    results = []
    results_lock = threading.Lock()
    start = time.time()

    # Export stays on a large thread pool, while opts.validator_workers
    # persistent threads (validator_worker_loop) each pull one
    # already-exported batch at a time off `work_queue` -- a *bounded*
    # queue.Queue, not a plain generator or an executor's own (unbounded)
    # internal queue: its maxsize is what stops the export producer from
    # racing arbitrarily far ahead and piling up unbounded bundle data in
    # tmpfs while validation lags behind. A small multiple of validator_workers keeps a little
    # look-ahead (so a worker never idles waiting on the next batch) without
    # letting the producer run away.
    work_queue = queue.Queue(maxsize=opts.validator_workers * 2)
    validator_threads = [
        threading.Thread(target=validator_worker_loop,
                          args=(work_queue, opts, reporter, results, results_lock),
                          daemon=True)
        for _ in range(opts.validator_workers)
    ]
    for t in validator_threads:
        t.start()

    export_pool = concurrent.futures.ThreadPoolExecutor(max_workers=opts.workers)
    try:
        for i, batch_files in enumerate(chunked(files, opts.batch_size)):
            batch_dir = run_root / f"batch-{i}"
            batch_dir.mkdir()
            file_results = {}
            bundle_to_result = {}
            futures = [export_pool.submit(process_file_phase1, f, batch_dir, opts.husk_bin, opts)
                       for f in batch_files]
            for fut in concurrent.futures.as_completed(futures):
                result, bundle_dir = fut.result()
                file_results[result.path] = result
                if bundle_dir is not None:
                    bundle_to_result[bundle_dir] = result

            work_queue.put((batch_dir, file_results, bundle_to_result))  # blocks once queue is full
    except KeyboardInterrupt:
        print("\ninterrupted -- reporting partial results...", file=sys.stderr)
    finally:
        export_pool.shutdown(wait=True, cancel_futures=True)
        for _ in validator_threads:
            work_queue.put(None)  # one stop sentinel per worker
        for t in validator_threads:
            t.join()
        reporter.close()
        # Aggressive cleanup: every batch directory is removed by its own
        # validator worker (see validate_and_report_batch's finally) the
        # instant that batch's results are recorded; this catches whatever
        # a cancelled/interrupted batch left behind so nothing from this
        # tool ever accumulates in /dev/shm across runs.
        shutil.rmtree(run_root, ignore_errors=True)

    elapsed = time.time() - start
    print_summary(results, elapsed, console)

    if opts.report:
        report = {
            "root": str(opts.root),
            "total_files": len(results),
            "elapsed_seconds": elapsed,
            "files": [
                {
                    "path": r.path,
                    "tests": [
                        {
                            "name": t.name,
                            "passed": t.passed,
                            "asserts": [
                                {"name": a.name, "passed": a.passed, "code": a.code, "detail": a.detail}
                                for a in t.asserts
                            ],
                        }
                        for t in r.tests
                    ],
                }
                for r in results
            ],
        }
        opts.report.write_text(json.dumps(report, indent=2))
        print(f"\nfull report written to {opts.report}")

    return 1 if any(r.test_fail > 0 for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
