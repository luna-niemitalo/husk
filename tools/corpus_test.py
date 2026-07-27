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
  - export: husk export produces a well-formed (correct magic, non-empty)
    .glb.
  - dump-chunks: (chunked files only) valid JSON output.
  - fidelity: the *independently* parsed M2 header's vertex/bone counts
    exactly match the exported glTF's actual accessor/skin counts -- same
    exact-match precedent tests/test_conformance.cpp's case-1/case-2
    checks established against curated fixtures, generalized to work
    without a hardcoded expected value.
  - finite: every FLOAT accessor (positions, normals, UVs, *and* every
    animation keyframe) is swept for NaN/Inf -- a static proxy for "doesn't
    explode when animated": a keyframe that's already non-finite at rest
    will definitely misbehave the moment anything interpolates it.
  - mesh_completeness: every primitive actually carries TEXCOORD_0 (UV
    data genuinely present, not just assumed).
  - validate: zero gltf_validator errors -- schema-level glTF correctness
    (joint hierarchy, weight normalization, etc.) is *not* reimplemented
    here; it's exactly what a mature, already-comprehensive validator is
    for.

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

Why gltf_validator runs are batched AND pooled, not one-per-file: gltf_validator
is a Dart program, and its per-invocation VM startup cost dominates its
actual (near-instant) validation work -- spawning it once per file
serializes the whole run behind that startup tax. It also supports pointing
at a *directory* and recursively validating every *.glb inside in one
process (one VM startup for the whole batch), writing a
`<name>.glb.report.json` sidecar per asset. Measured empirically on this
machine (32 cores): validating a fixed corpus with 1 process takes about
the same wall time as splitting it 8 ways across 8 concurrent processes
(no speedup at all -- Dart VMs are heavy enough that 8 concurrent instances
hit real contention), but running the *same full corpus* through 2 or 4
concurrent processes scales almost linearly. So: husk's own info/export/
dump-chunks run on a large thread pool (they scale cleanly, no such
ceiling); gltf_validator runs on a small, fixed-size pool of persistent
worker threads (--validator-workers, default 4), each pulling one
already-exported batch at a time off a *bounded* queue.Queue so the export
producer can't race arbitrarily far ahead of validation and pile up
unbounded .glb data in tmpfs.
"""

import argparse
import concurrent.futures
import json
import math
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
DEFAULT_CORPUS_ROOT = Path("/media/luna/data/wow_export")


@dataclass
class AssertResult:
    name: str
    passed: bool
    code: str = ""     # short, groupable failure category (e.g. a gltf_validator "code")
    detail: str = ""   # only populated on failure -- no point keeping noise for a pass


@dataclass
class TestResult:
    name: str
    asserts: list  # list[AssertResult]

    @property
    def passed(self) -> bool:
        return all(a.passed for a in self.asserts)


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


def test_export(husk_bin, m2_path, out_glb, anim_mode, timeout):
    cmd = [
        str(husk_bin), "export", str(m2_path), "-o", str(out_glb),
        "--textures", "none", "--anim", anim_mode, "--bones-dir", "none",
    ]
    code, out = run_cmd(cmd, timeout)
    exists = out_glb.exists()
    size_ok = exists and out_glb.stat().st_size > 12
    magic_ok = False
    if size_ok:
        with open(out_glb, "rb") as f:
            magic_ok = f.read(4) == b"glTF"
    asserts = [
        AssertResult("exit_code_0", code == 0, detail=last_meaningful_line(out) if code != 0 else ""),
        AssertResult("glb_written", exists, detail="" if exists else "no output file"),
        AssertResult("glb_nonempty", size_ok, detail="" if size_ok else "empty or missing"),
        AssertResult("glb_magic", magic_ok, detail="" if magic_ok else "bad glTF magic bytes"),
    ]
    return TestResult("export", asserts), exists


def test_dump_chunks(husk_bin, m2_path, timeout):
    code, out = run_cmd([str(husk_bin), "dump-chunks", str(m2_path)], timeout)
    looks_json = out.strip().startswith("{")
    asserts = [
        AssertResult("exit_code_0", code == 0, detail=last_meaningful_line(out) if code != 0 else ""),
        AssertResult("looks_like_json", looks_json, detail="" if looks_json else out[:200]),
    ]
    return TestResult("dump-chunks", asserts)


# ---------------------------------------------------------------------------
# glTF content correctness (fidelity/finite/mesh_completeness): a minimal,
# from-scratch glb reader -- deliberately not a dependency like pygltflib,
# since all that's needed is "read this accessor's raw values," and every
# glb husk produces uses plain (non-sparse), single-bufferView accessors.
# Sparse accessors are recognized and skipped (husk doesn't emit them; a
# skip here just means "not checked," never a silent misread).
# ---------------------------------------------------------------------------

_COMPONENT_TYPES = {
    5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
    5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4),
}
_TYPE_COMPONENT_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}


def read_glb(glb_path: Path):
    data = glb_path.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF":
        raise ValueError("bad glb magic")
    off = 12
    gltf_json, bin_chunk = None, b""
    while off + 8 <= length:
        chunk_len, chunk_type = struct.unpack_from("<I4s", data, off)
        off += 8
        chunk_data = data[off:off + chunk_len]
        if chunk_type == b"JSON":
            gltf_json = json.loads(chunk_data)
        elif chunk_type == b"BIN\x00":
            bin_chunk = chunk_data
        off += chunk_len
    if gltf_json is None:
        raise ValueError("no JSON chunk in glb")
    return gltf_json, bin_chunk


def read_accessor_values(gltf, bin_chunk, accessor_index):
    """Returns a flat list of component values for the accessor, or None if
    it's a shape this reader deliberately doesn't handle (sparse, or no
    bufferView -- the latter is spec-valid (implicit zeros) but husk never
    emits it, so treating it as "not checked" rather than guessing is safer.
    """
    acc = gltf["accessors"][accessor_index]
    if "sparse" in acc or "bufferView" not in acc:
        return None
    comp_fmt, comp_size = _COMPONENT_TYPES[acc["componentType"]]
    n_comp = _TYPE_COMPONENT_COUNTS[acc["type"]]
    bv = gltf["bufferViews"][acc["bufferView"]]
    base_offset = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = bv.get("byteStride", n_comp * comp_size)
    count = acc["count"]
    fmt = f"<{n_comp}{comp_fmt}"
    values = []
    for i in range(count):
        values.extend(struct.unpack_from(fmt, bin_chunk, base_offset + i * stride))
    return values


def collision_mesh_indices(gltf) -> set:
    # The collision mesh (if present) is a genuinely separate NamedMesh with
    # its own, unrelated, much smaller vertex buffer and no UV data at all
    # (see cmd_export.cpp/gltf.cpp's isCollision handling) -- tagged via
    # {"collision": true} on its *node*, not the mesh itself. Both
    # test_fidelity (vertex-count consistency) and test_mesh_completeness
    # (UV presence) need to exclude it, or every collision-bearing export
    # would falsely look like a regression.
    return {
        node["mesh"] for node in gltf.get("nodes", [])
        if node.get("extras", {}).get("collision") is True and "mesh" in node
    }


def test_fidelity(gltf, header_summary):
    """Cross-checks husk's exported glTF against an *independently* parsed
    M2 header -- not a hardcoded expected value, so this generalizes to any
    file. Exact match, no tolerance: same precedent as
    tests/test_conformance.cpp's case-1/case-2 checks (vertex count, bone
    count) against real fixtures -- see CLAUDE.md's Resume for how those
    were established and why exact (not "> 0") is the right bar here.
    """
    asserts = []
    if header_summary is None:
        return None  # independent parse itself failed -- nothing to cross-check
    meshes = gltf.get("meshes", [])
    accessors = gltf.get("accessors", [])
    excluded = collision_mesh_indices(gltf)
    position_counts = {
        accessors[prim["attributes"]["POSITION"]]["count"]
        for mi, mesh in enumerate(meshes) if mi not in excluded
        for prim in mesh.get("primitives", [])
        if "POSITION" in prim.get("attributes", {})
    }
    # All primitives share one vertex buffer in husk's own design (only
    # `indices` differs per submesh/batch) -- so every primitive's POSITION
    # accessor should report the *same* count; more than one distinct value
    # here would itself be a real regression, not just a fidelity mismatch.
    if position_counts:
        consistent = len(position_counts) == 1
        asserts.append(AssertResult("one_consistent_vertex_count", consistent,
                                     detail="" if consistent
                                     else f"primitives disagree on vertex count: {position_counts}"))
        vertex_count = next(iter(position_counts)) if consistent else None
        if vertex_count is not None:
            asserts.append(AssertResult(
                "vertex_count_matches_header", vertex_count == header_summary["vertex_count"],
                detail="" if vertex_count == header_summary["vertex_count"]
                else f"glb has {vertex_count}, header says {header_summary['vertex_count']}"))

    # header_summary["bone_count"] == 0 legitimately means "bones live in an
    # external .skel file, not inline" (husk export --skel; see DESIGN.md's
    # CLI grammar section) -- this independent reader doesn't parse .skel,
    # so it has no expected count to check in that case. Only assert when
    # the M2 itself claims inline bones.
    skins = gltf.get("skins", [])
    if skins and header_summary["bone_count"] > 0:
        joint_count = len(skins[0].get("joints", []))
        asserts.append(AssertResult(
            "bone_count_matches_header", joint_count == header_summary["bone_count"],
            detail="" if joint_count == header_summary["bone_count"]
            else f"glb skin has {joint_count} joints, header says {header_summary['bone_count']} bones"))

    return TestResult("fidelity", asserts) if asserts else None


def test_finite(gltf, bin_chunk):
    """Sweeps every FLOAT accessor -- vertex positions/normals/UVs *and*
    every animation sampler's keyframe input/output, since those are all
    just entries in the same top-level `accessors` array -- for NaN/Inf.
    This is a static check on stored data, not a full animation-playback
    simulation, but a keyframe that's already non-finite at rest will
    definitely "explode" the moment anything samples/interpolates it.
    """
    bad = []
    for i, acc in enumerate(gltf.get("accessors", [])):
        if acc.get("componentType") != 5126:  # FLOAT only
            continue
        values = read_accessor_values(gltf, bin_chunk, i)
        if values is None:
            continue
        if not all(math.isfinite(v) for v in values):
            bad.append(i)
    passed = not bad
    return TestResult("finite", [AssertResult(
        "all_float_accessors_finite", passed,
        detail="" if passed else f"non-finite values in accessor(s) {bad[:10]}")])


def test_mesh_completeness(gltf):
    """Does every primitive actually carry UV data, not just claim to via a
    material reference -- a real "meshes have UV information" check, not a
    guess. husk always exports UVs when the M2 vertex format has them
    (effectively always for real character/creature/item models), so a
    primitive missing TEXCOORD_0 is worth a look, not expected noise.
    """
    excluded = collision_mesh_indices(gltf)  # never textured/UV-mapped by design
    missing = []
    for mi, mesh in enumerate(gltf.get("meshes", [])):
        if mi in excluded:
            continue
        for pi, prim in enumerate(mesh.get("primitives", [])):
            if "TEXCOORD_0" not in prim.get("attributes", {}):
                missing.append(f"mesh{mi}/prim{pi}")
    passed = not missing
    return TestResult("mesh_completeness", [AssertResult(
        "every_primitive_has_texcoord0", passed,
        detail="" if passed else f"missing on {missing[:10]}")])


def test_glb_content(glb_path: Path, header_summary):
    """Runs fidelity/finite/mesh_completeness together against one glb parse
    (avoids reading+decoding the same file 3 times). Returns the list of
    TestResults that actually apply -- a glb this reader can't even open
    (should be unreachable if export's own magic-byte check passed, but
    defensive regardless) reports that failure once instead of silently
    skipping every content check.
    """
    try:
        gltf, bin_chunk = read_glb(glb_path)
    except (ValueError, KeyError, struct.error, json.JSONDecodeError) as e:
        return [TestResult("glb_content", [AssertResult("glb_parseable", False, detail=str(e))])]

    results = []
    fidelity = test_fidelity(gltf, header_summary)
    if fidelity is not None:
        results.append(fidelity)
    results.append(test_finite(gltf, bin_chunk))
    results.append(test_mesh_completeness(gltf))
    return results


def validate_result_from_report(report_path: Path, run_out: str) -> TestResult:
    # gltf_validator's own documented contract: nonzero process exit iff
    # >= 1 error anywhere in the batch -- that tells us nothing about
    # *this* file, so read this file's own report.json (issues.numErrors/
    # messages) instead of the batch-wide exit code.
    if not report_path.exists():
        return TestResult("validate", [AssertResult(
            "zero_errors", False, code="NO_REPORT",
            detail=f"no report.json produced (validator output: {last_meaningful_line(run_out)})")])
    try:
        report = json.loads(report_path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        return TestResult("validate", [AssertResult("zero_errors", False, code="BAD_REPORT", detail=str(e))])
    issues = report.get("issues", {})
    errors = [m for m in issues.get("messages", []) if m.get("severity") == 0]
    if not errors:
        return TestResult("validate", [AssertResult("zero_errors", True)])
    first = errors[0]
    return TestResult("validate", [AssertResult(
        "zero_errors", False, code=first.get("code", "?"),
        detail=f"{first.get('code')}: {first.get('message')} ({first.get('pointer')})"
               + (f" [+{len(errors) - 1} more]" if len(errors) > 1 else ""))])


_name_counter = 0
_name_counter_lock = threading.Lock()


def unique_glb_path(batch_dir, m2_path):
    global _name_counter
    with _name_counter_lock:
        _name_counter += 1
        n = _name_counter
    return batch_dir / f"{n}_{Path(m2_path).stem}.glb"


def process_file_phase1(m2_path, batch_dir, husk_bin, opts):
    """Runs every check that's fast/native and scales cleanly with a large
    thread pool: husk info/export/dump-chunks, plus the glb content checks
    (fidelity/finite/mesh_completeness -- pure Python, no subprocess, so
    they belong here, not on the small gltf_validator worker pool). Does
    NOT run gltf_validator itself -- that's batched separately (see module
    docstring) -- and does NOT delete the .glb: the caller needs it around
    for the batch-wide validator pass, and cleans up the whole batch
    directory afterward instead.
    """
    result = FileResult(path=str(m2_path))
    glb_path = unique_glb_path(batch_dir, m2_path)

    header_result, is_chunked, header_summary = test_header(husk_bin, m2_path, opts.timeout)
    result.tests.append(header_result)

    export_result, glb_exists = test_export(husk_bin, m2_path, glb_path, opts.anim, opts.timeout)
    result.tests.append(export_result)

    if is_chunked and not opts.skip_dump:
        result.tests.append(test_dump_chunks(husk_bin, m2_path, opts.timeout))

    if glb_exists and not opts.skip_content_checks:
        result.tests.extend(test_glb_content(glb_path, header_summary))

    return result, (glb_path if glb_exists else None)


def validate_and_report_batch(batch_dir, file_results, glb_to_result, gltf_validator_bin,
                               opts, reporter, results, results_lock):
    """Owns one batch's validation, reporting, and cleanup end to end -- the
    unit of work a validator worker pulls off the bounded queue (see
    validator_worker_loop/main()). Runs exactly one gltf_validator
    invocation for the whole batch (amortizing its Dart VM startup cost
    across every file in it, see module docstring), merges each file's own
    report.json back into its FileResult, then reports and records every
    file in this batch itself: this worker owns this batch's output start
    to finish, so multiple validator workers running concurrently never
    contend over a single central "merge results" step -- only the shared
    `results` list append is (briefly) locked, and the printed lines/
    progress bar go through tqdm/rich, both already safe for concurrent use
    from worker threads.
    """
    try:
        if not opts.skip_validator and gltf_validator_bin and glb_to_result:
            code, out = run_cmd([str(gltf_validator_bin), "-a", str(batch_dir)], opts.validator_timeout)
            for glb_path, result in glb_to_result.items():
                report_path = glb_path.with_name(glb_path.name + ".report.json")
                result.tests.append(validate_result_from_report(report_path, out))
        with results_lock:
            results.extend(file_results.values())
        for result in file_results.values():
            reporter.report(result, opts.root)
    finally:
        shutil.rmtree(batch_dir, ignore_errors=True)


def validator_worker_loop(work_queue, gltf_validator_bin, opts, reporter, results, results_lock):
    """Runs forever in its own thread, pulling one already-exported batch at
    a time off `work_queue` -- one persistent 'parser' per worker, matching
    export's own per-file worker shape but at batch granularity, since
    gltf_validator's fixed per-invocation cost (see module docstring) needs
    many files per invocation to amortize well. `work_queue` is a *bounded*
    queue.Queue (see main()): its own put()/get() blocking is what keeps the
    export producer from racing arbitrarily far ahead and piling up
    unbounded .glb data in tmpfs -- a plain generator can lazily produce
    batches, but only a bounded queue between threads actually throttles
    the producer against how fast these workers are draining it.
    """
    while True:
        item = work_queue.get()
        try:
            if item is None:  # sentinel: no more batches, this worker is done
                return
            batch_dir, file_results, glb_to_result = item
            validate_and_report_batch(batch_dir, file_results, glb_to_result,
                                       gltf_validator_bin, opts, reporter, results, results_lock)
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
    ap.add_argument("--batch-size", type=int, default=500,
                     help="files per gltf_validator batch (one Dart VM startup per batch, not per file)")
    ap.add_argument("--validator-workers", type=int, default=4,
                     help="concurrent gltf_validator processes (default: 4 -- empirically the sweet spot on "
                          "this machine; unlike husk itself, running many more concurrent Dart VMs measured "
                          "no faster than 1, see module docstring)")
    ap.add_argument("--limit", type=int, default=None, help="only process the first N discovered files")
    ap.add_argument("--sample", type=int, default=None, help="randomly sample N files instead of all")
    ap.add_argument("--seed", type=int, default=0, help="RNG seed for --sample (reproducible by default)")
    ap.add_argument("--anim", default="auto", choices=["auto", "inline", "none"],
                     help="--anim mode passed to husk export (default: auto, most thorough; "
                          "use 'inline' for a faster pass that skips external .anim directory search)")
    ap.add_argument("--timeout", type=float, default=30.0, help="per-husk-invocation timeout, seconds")
    ap.add_argument("--validator-timeout", type=float, default=120.0, help="per-batch gltf_validator timeout, seconds")
    ap.add_argument("--skip-validator", action="store_true", help="skip the gltf_validator pass entirely")
    ap.add_argument("--skip-dump", action="store_true", help="skip the dump-chunks pass on chunked files")
    ap.add_argument("--skip-content-checks", action="store_true",
                     help="skip fidelity/finite/mesh_completeness glb content checks")
    ap.add_argument("--ram-dir", type=Path, default=Path("/dev/shm"),
                     help="tmpfs root for scratch .glb output (default: /dev/shm)")
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

    gltf_validator_bin = None if opts.skip_validator else shutil.which("gltf_validator")
    if not opts.skip_validator and not gltf_validator_bin:
        print("warning: gltf_validator not found on PATH -- validate pass will be skipped for every file",
              file=sys.stderr)

    use_color = (not opts.no_color) and sys.stdout.isatty()
    console = Console(force_terminal=use_color, no_color=not use_color, highlight=False)

    print(f"discovering files under {opts.root} (pattern {opts.pattern!r})...", file=sys.stderr)
    files = discover_files(opts.root, opts.pattern, opts.limit, opts.sample, opts.seed)
    total = len(files)
    if total == 0:
        print("no files matched -- nothing to do", file=sys.stderr)
        return 0
    print(f"{total} file(s) to test, {opts.workers} husk worker(s), "
          f"{opts.validator_workers} gltf_validator worker(s) x {opts.batch_size} files/batch, "
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

    # husk's info/export/dump-chunks are cheap native calls that scale
    # cleanly with --workers (measured ~218 files/s at 32 workers on this
    # machine); gltf_validator does not -- it's a Dart program, and
    # empirically (see module docstring) running more than a handful
    # concurrently hits a wall on this machine (8 concurrent instances
    # measured no faster than 1; 2-4 scaled almost linearly). So export
    # stays on a large thread pool, while opts.validator_workers persistent
    # threads (validator_worker_loop) each pull one already-exported batch
    # at a time off `work_queue` -- a *bounded* queue.Queue, not a plain
    # generator or an executor's own (unbounded) internal queue: its
    # maxsize is what stops the export producer from racing arbitrarily far
    # ahead and piling up unbounded .glb data in tmpfs while validation
    # lags behind. A small multiple of validator_workers keeps a little
    # look-ahead (so a worker never idles waiting on the next batch) without
    # letting the producer run away.
    work_queue = queue.Queue(maxsize=opts.validator_workers * 2)
    validator_threads = [
        threading.Thread(target=validator_worker_loop,
                          args=(work_queue, gltf_validator_bin, opts, reporter, results, results_lock),
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
            glb_to_result = {}
            futures = [export_pool.submit(process_file_phase1, f, batch_dir, opts.husk_bin, opts)
                       for f in batch_files]
            for fut in concurrent.futures.as_completed(futures):
                result, glb_path = fut.result()
                file_results[result.path] = result
                if glb_path is not None:
                    glb_to_result[glb_path] = result

            work_queue.put((batch_dir, file_results, glb_to_result))  # blocks once queue is full
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
