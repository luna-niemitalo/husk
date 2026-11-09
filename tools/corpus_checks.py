#!/usr/bin/env python3
"""Library of idempotent, per-file M2 corpus checks.

Every public check function below
takes exactly one argument, an `.m2` path, and its only output is a
read-modify-write of that file's own status JSON under STATUS_DIR -- never
shared state, never another file's status JSON. That's what makes them
safe to call for many different files fully in parallel: two different
`.m2` paths never touch the same status JSON, so there is no write race no
matter how heavily a caller's scheduler parallelizes across *files*.

What isn't guaranteed in parallel: running two checks for the *same* file
concurrently. Consecutive checks on one file (see TEST_ORDER) read and
overwrite that one file's status JSON with no locking -- by design, since
the intended usage is one file's checks running strictly one after another
(whatever schedules across files is the caller's job, not this library's).

Everything a check needs beyond the file path itself comes from this
module's own globals (CORPUS_ROOT, STATUS_DIR, SCRATCH_DIR, HUSK_BIN, ...)
-- set them once before calling anything; see corpus_checks_example.py for
a minimal end-to-end usage.

validate is the one check split into two halves on purpose: producing a
`husk validate-bundle` report (run_bundle_validator) is a batchable,
orchestration-level concern -- one invocation takes any number of bundle
directories and pays its startup and embedded-schema load once (see
run_bundle_validator's own docstring) -- while consuming that report into
a file's status JSON (test_validate) stays a plain, idempotent,
single-file-path check like everything else. Building the batching itself
is the caller's call.

The bundle content checks (fidelity/finite/mesh_completeness) exist as
pure functions over an already-read manifest (*_result below) so
corpus_test.py runs the same checks without the status-JSON plumbing.
"""

import json
import math
import re
import shutil
import struct
import subprocess
from array import array
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# --- caller-configured globals ------------------------------------------
# Set these once, before calling any check. A scheduler driving many
# worker threads/processes can share one import of this module read-only
# after that -- nothing here is ever written per-file.
CORPUS_ROOT: Path | None = None            # root every mirrored path is relative to
STATUS_DIR: Path | None = None             # per-file <name>.status.json, mirrored under here
SCRATCH_DIR: Path | None = None            # per-file scratch bundle dir + validator report, mirrored
# PATH first, falling back to the local build path: the flake dev shell
# provides husk's build tools, not a built husk, so a bare "husk" alone
# would break every invocation there.
HUSK_BIN = shutil.which("husk") or str(REPO_ROOT / "build" / "husk")
TIMEOUT = 30.0                             # seconds, per husk invocation
VALIDATOR_TIMEOUT = 120.0                  # seconds, per `husk validate-bundle` invocation
ANIM_MODE = "auto"                         # passed straight through to `husk export --anim`


@dataclass
class AssertResult:
    name: str
    passed: bool
    code: str = ""     # short, groupable failure category
    detail: str = ""   # only populated on failure


@dataclass
class TestResult:
    name: str
    asserts: list  # list[AssertResult]

    @property
    def passed(self) -> bool:
        return all(a.passed for a in self.asserts)


# ---------------------------------------------------------------------------
# Path mirroring + status JSON
# ---------------------------------------------------------------------------

def _require_config() -> None:
    missing = [name for name in ("CORPUS_ROOT", "STATUS_DIR", "SCRATCH_DIR") if globals()[name] is None]
    if missing:
        raise RuntimeError(f"corpus_checks.{'/'.join(missing)} must be set before calling any check")


def _relative(m2_path) -> Path:
    """Every mirrored status/scratch path derives from this -- the shared
    key that keeps two same-named files in different corpus subdirectories
    from colliding, and the reason status/scratch paths stay human-browsable
    (mirroring the corpus's own directory tree) instead of a flat hash.
    """
    _require_config()
    return Path(m2_path).resolve().relative_to(Path(CORPUS_ROOT).resolve())


def status_path(m2_path) -> Path:
    rel = _relative(m2_path)
    return STATUS_DIR / rel.parent / (rel.name + ".status.json")


def scratch_bundle_path(m2_path) -> Path:
    rel = _relative(m2_path)
    return SCRATCH_DIR / rel.parent / (rel.name + ".bundle")


def validator_report_path(bundle_dir) -> Path:
    # A sibling, not inside the bundle: `husk export` writes over the
    # bundle directory, and the report must not look like a bundle file.
    bundle_dir = Path(bundle_dir)
    return bundle_dir.with_name(bundle_dir.name + ".validate.json")


def _load_status(m2_path) -> dict:
    p = status_path(m2_path)
    if not p.exists():
        return {"path": str(m2_path)}
    return json.loads(p.read_text())


def _save_status(m2_path, status: dict) -> None:
    p = status_path(m2_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(status, indent=2))


def _record(m2_path, result: TestResult) -> bool:
    """Read-modify-write this file's status json: sets/replaces this
    check's entry by name, so reruns overwrite cleanly instead of
    accumulating duplicates -- idempotent means idempotent.
    """
    status = _load_status(m2_path)
    status.setdefault("tests", {})[result.name] = {
        "passed": result.passed,
        "asserts": [
            {"name": a.name, "passed": a.passed, "code": a.code, "detail": a.detail}
            for a in result.asserts
        ],
    }
    _save_status(m2_path, status)
    return result.passed


# ---------------------------------------------------------------------------
# Subprocess + independent (non-husk) M2 header parse
# ---------------------------------------------------------------------------

def run_cmd(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except subprocess.TimeoutExpired:
        return -1, f"TIMEOUT after {timeout}s: {' '.join(str(c) for c in cmd)}"
    except OSError as e:
        return -2, f"OSError launching {cmd[0]}: {e}"


def _last_meaningful_line(out: str) -> str:
    # husk always prints its actual error/failure message as the final
    # line before a nonzero exit (see main.cpp's top-level catch).
    for line in reversed(out.splitlines()):
        if line.strip():
            return line.strip()
    return ""


# Same fixed field offsets husk's own src/m2.cpp uses (see WIKI_FINDINGS.md)
# -- reimplemented from scratch here, not calling husk, so this is a
# genuinely independent second opinion, not husk checking its own homework.
_M2_VERSION_OFFSET = 0x04
_M2_BONES_COUNT_OFFSET = 0x02C
_M2_VERTICES_COUNT_OFFSET = 0x03C
_M2_MIN_HEADER_SIZE = 0x090


def read_m2_header_summary(m2_path):
    """Independent (no husk involved) minimal M2 header read: magic/chunk
    container walk + a handful of fixed-offset fields. Returns None if the
    file isn't even recognizable as MD20/MD21-wrapped.
    """
    data = Path(m2_path).read_bytes()
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
# corpus. Order matters: first match wins, most-specific patterns first.
_FAILURE_CATEGORIES = [
    ("TRUNCATED_FILE", ("too short to even contain a magic value",)),
    ("NOT_RECOGNIZED_AS_M2", ("doesn't start with md20", "no md21 chunk")),
    ("CHUNK_CONTAINER_TRUNCATED", ("truncated chunk header", "declares size")),
    ("ARRAY_BOUNDS_VIOLATION", ("out of range", "needs more room", "claims")),
    ("SKIN_MISMATCH", ("skin index", "out of range for the skin")),
]


def _classify_parse_failure(message: str) -> str:
    lower = message.lower()
    for category, needles in _FAILURE_CATEGORIES:
        if any(n in lower for n in needles):
            return category
    return "OTHER_PARSE_ERROR"


# ---------------------------------------------------------------------------
# Checks: each takes exactly one argument (m2_path) and returns True/False
# (overall pass, for convenience) or None when a prerequisite from an
# earlier check in TEST_ORDER hasn't run yet -- None is "not applicable
# yet", never recorded to the status json, and distinct from a real failure.
# ---------------------------------------------------------------------------

def test_header(m2_path) -> bool:
    """Not just "did husk info exit 0" -- classifies *why* when it doesn't,
    and cross-checks against an independent (non-husk) parse of the same
    file.
    """
    code, out = run_cmd([str(HUSK_BIN), "info", str(m2_path)], TIMEOUT)
    asserts = []
    if code == 0:
        asserts.append(AssertResult("recognized", True))
        asserts.append(AssertResult("has_version_line", "version:" in out,
                                     detail="" if "version:" in out else out[:500]))
        independent = read_m2_header_summary(m2_path)
        if independent is not None:
            plausible = 0 <= independent["vertex_count"] <= 10_000_000
            asserts.append(AssertResult(
                "independent_vertex_count_plausible", plausible,
                detail="" if plausible else f"independent parse got vertex_count={independent['vertex_count']}"))
    else:
        reason = _last_meaningful_line(out)
        asserts.append(AssertResult("recognized", False, code=_classify_parse_failure(reason), detail=reason))
    return _record(m2_path, TestResult("header", asserts))


def _export_result(m2_path, bundle_dir: Path, extra_args) -> TestResult:
    """Shared by test_export and m2_full_validation_task's rich export:
    the scratch bundle is cleared first so a rerun never reads a previous
    run's leftover bin (husk writes over a bundle but doesn't empty it).
    """
    shutil.rmtree(bundle_dir, ignore_errors=True)
    bundle_dir.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(HUSK_BIN), "export", str(m2_path), "-o", str(bundle_dir), *extra_args]
    code, out = run_cmd(cmd, TIMEOUT)
    manifest_path = bundle_dir / "manifest.json"
    written = manifest_path.exists()
    kind = None
    parse_error = "no manifest.json"
    if written:
        try:
            kind = json.loads(manifest_path.read_bytes()).get("kind")
            parse_error = ""
        except (OSError, json.JSONDecodeError, AttributeError) as e:
            parse_error = str(e)
    return TestResult("export", [
        AssertResult("exit_code_0", code == 0, detail=_last_meaningful_line(out) if code != 0 else ""),
        AssertResult("manifest_written", written, detail="" if written else "no manifest.json in the bundle"),
        AssertResult("manifest_parseable", not parse_error, detail=parse_error),
        AssertResult("manifest_kind_model", kind == "model",
                     detail="" if kind == "model" else f"expected kind 'model', got {kind!r}"),
    ])


def test_export(m2_path) -> bool:
    bundle_dir = scratch_bundle_path(m2_path)
    args = ["--textures", "none", "--anim", ANIM_MODE, "--bones-dir", "none"]
    return _record(m2_path, _export_result(m2_path, bundle_dir, args))


def test_dump_chunks(m2_path) -> bool:
    """Runs unconditionally on every file, chunked or not: src/cmd_dump.cpp's
    `header.chunked` early-return only gates the Legion+ chunk-tag section
    -- ribbon/particle-emitter JSON is emitted for every M2 version -- so
    there's no format this can be N/A for.
    """
    code, out = run_cmd([str(HUSK_BIN), "dump-chunks", str(m2_path)], TIMEOUT)
    looks_json = out.strip().startswith("{")
    asserts = [
        AssertResult("exit_code_0", code == 0, detail=_last_meaningful_line(out) if code != 0 else ""),
        AssertResult("looks_like_json", looks_json, detail="" if looks_json else out[:200]),
    ]
    return _record(m2_path, TestResult("dump-chunks", asserts))


# --- bundle reader ------------------------------------------------------
# From scratch, no dependency: a bundle is one JSON manifest plus raw
# little-endian bins, and every array in a bin is a buffer_slice
# (schema/common.schema.json): {file, byte_offset, byte_length,
# component_type, component_count, count}. Not husk's own reader, so the
# content checks below are a second opinion, not husk checking itself.

_COMPONENT_FORMATS = {"u8": "B", "u16": "H", "u32": "I", "i32": "i", "f32": "f"}
_SLICE_KEYS = {"file", "byte_offset", "byte_length", "component_type", "component_count", "count"}


def read_manifest(bundle_dir) -> dict:
    return json.loads((Path(bundle_dir) / "manifest.json").read_bytes())


def buffer_slices(node):
    """Every buffer_slice anywhere in a manifest subtree, in document order."""
    if isinstance(node, dict):
        if _SLICE_KEYS <= node.keys():
            yield node
            return
        for value in node.values():
            yield from buffer_slices(value)
    elif isinstance(node, list):
        for value in node:
            yield from buffer_slices(value)


def read_slice(bundle_dir, slice_, bins: dict) -> array:
    """The slice's flat component values. `bins` caches each bin file's
    bytes across calls (file name -> bytes). Raises ValueError on a slice
    whose declared shape doesn't fit its bin -- the expected/actual sizes
    in the message are the finding.
    """
    fmt = _COMPONENT_FORMATS.get(slice_["component_type"])
    if fmt is None:
        raise ValueError(f"{slice_['file']}@{slice_['byte_offset']}: component_type "
                         f"{slice_['component_type']!r} not one of {sorted(_COMPONENT_FORMATS)}")
    values = array(fmt)
    expected_length = slice_["count"] * slice_["component_count"] * values.itemsize
    if slice_["byte_length"] != expected_length:
        raise ValueError(f"{slice_['file']}@{slice_['byte_offset']}: byte_length {slice_['byte_length']}, "
                         f"expected count*component_count*size = {expected_length}")
    name = slice_["file"]
    if name not in bins:
        bins[name] = (Path(bundle_dir) / name).read_bytes()
    data = bins[name]
    start, end = slice_["byte_offset"], slice_["byte_offset"] + slice_["byte_length"]
    if end > len(data):
        raise ValueError(f"{name}@{start}: slice ends at byte {end}, file has {len(data)} bytes")
    values.frombytes(data[start:end])
    return values


def _mesh(manifest):
    return manifest.get("resources", {}).get("geometry", {}).get("mesh")


def _vertex_streams(mesh) -> dict:
    """Every per-vertex stream of one mesh or mesh level, by name."""
    streams = {name: mesh[name] for name in ("positions", "normals", "joints0", "weights0") if name in mesh}
    streams.update({f"uv_sets[{k}]": s for k, s in enumerate(mesh.get("uv_sets", []))})
    streams.update({f"colors[{k}]": s for k, s in enumerate(mesh.get("colors", []))})
    return streams


def fidelity_result(manifest, header_summary):
    """Cross-checks the bundle against an *independently* parsed M2 header
    -- not a hardcoded expected value, so this generalizes to any file.
    Exact match, no tolerance (same bar as tests/test_conformance.cpp's
    fixture checks). None when the independent parse failed: nothing to
    cross-check against.
    """
    if header_summary is None:
        return None
    asserts = []
    mesh = _mesh(manifest)
    vertex_count = 0
    if mesh is not None:
        counts = {name: s["count"] for name, s in _vertex_streams(mesh).items()}
        consistent = len(set(counts.values())) == 1
        asserts.append(AssertResult("vertex_streams_agree", consistent,
                                    detail="" if consistent else f"per-vertex stream counts differ: {counts}"))
        vertex_count = mesh["positions"]["count"]
    # A bundle with no mesh at all must come from an M2 that declares no
    # vertices; geometry-less models are real (a whole corpus class).
    asserts.append(AssertResult(
        "vertex_count_matches_header", vertex_count == header_summary["vertex_count"],
        detail="" if vertex_count == header_summary["vertex_count"]
        else f"bundle mesh has {vertex_count} vertices, header says {header_summary['vertex_count']}"))

    # bone_count == 0 means the bones live in an external .skel, which this
    # independent reader doesn't parse -- no expected count to check then.
    if header_summary["bone_count"] > 0:
        skeleton = manifest.get("resources", {}).get("rig", {}).get("skeleton")
        joint_count = skeleton["joint_count"] if skeleton else 0
        asserts.append(AssertResult(
            "bone_count_matches_header", joint_count == header_summary["bone_count"],
            detail="" if joint_count == header_summary["bone_count"]
            else f"bundle skeleton has {joint_count} joints, header says {header_summary['bone_count']} bones"))
    return TestResult("fidelity", asserts)


def finite_result(bundle_dir, manifest) -> TestResult:
    """Sweeps every f32 buffer_slice the manifest names -- mesh and every
    LOD level, skeleton, every animation curve's times and values,
    collision -- for NaN/Inf. A static check on stored data, not playback,
    but a keyframe that's non-finite at rest will "explode" the moment
    anything interpolates it. Slices shared between LOD levels are read
    once.
    """
    bins = {}
    seen = set()
    unreadable, bad = [], []
    for s in buffer_slices(manifest):
        key = (s["file"], s["byte_offset"], s["byte_length"], s["component_type"])
        if key in seen:
            continue
        seen.add(key)
        try:
            values = read_slice(bundle_dir, s, bins)
        except (OSError, ValueError) as e:
            unreadable.append(str(e))
            continue
        if s["component_type"] == "f32" and not all(math.isfinite(v) for v in values):
            bad.append(f"{s['file']}@{s['byte_offset']}")
    return TestResult("finite", [
        AssertResult("all_slices_readable", not unreadable,
                     detail="" if not unreadable else f"{len(unreadable)} unreadable: {unreadable[:3]}"),
        AssertResult("all_f32_slices_finite", not bad,
                     detail="" if not bad else f"non-finite values in slice(s) {bad[:10]}"),
    ])


def mesh_completeness_result(manifest):
    """Every M2 vertex carries UV set 0 (the vertex record has fixed UV
    fields), so the mesh and every LOD level must hold a UV set 0 as long
    as their positions; and every primitive, at every level, must name a
    material the bundle actually has. None for a bundle with no mesh.
    """
    mesh = _mesh(manifest)
    if mesh is None:
        return None
    lod = manifest["resources"]["geometry"].get("lod", {})
    levels = [("mesh", mesh)] + [(f"lod{i}", level) for i, level in enumerate(lod.get("levels", []))]
    material_count = len(manifest["resources"].get("materials", []))
    missing_uv, bad_material = [], []
    for name, level in levels:
        uv_sets = level.get("uv_sets", [])
        if not uv_sets or uv_sets[0]["count"] != level["positions"]["count"]:
            missing_uv.append(f"{name}: positions {level['positions']['count']}, "
                              f"uv_sets[0] {uv_sets[0]['count'] if uv_sets else 'absent'}")
        for pi, prim in enumerate(level.get("primitives", [])):
            index = prim.get("material_index")
            if index is None or not 0 <= index < material_count:
                bad_material.append(f"{name}/prim{pi}: material_index {index}, {material_count} materials")
    return TestResult("mesh_completeness", [
        AssertResult("uv_set_0_covers_vertices", not missing_uv,
                     detail="" if not missing_uv else "; ".join(missing_uv[:10])),
        AssertResult("every_primitive_has_material", not bad_material,
                     detail="" if not bad_material else "; ".join(bad_material[:10])),
    ])


def _load_bundle_for_content_check(m2_path):
    """Shared by test_fidelity/test_finite/test_mesh_completeness. Returns
    (bundle_dir, manifest), or None if the bundle isn't there yet
    (test_export hasn't run) or its manifest doesn't parse -- test_export
    already records that failure.
    """
    bundle_dir = scratch_bundle_path(m2_path)
    try:
        return bundle_dir, read_manifest(bundle_dir)
    except (OSError, json.JSONDecodeError):
        return None


def test_fidelity(m2_path):
    """None (no status entry written) when the bundle or the independent
    header parse isn't available yet -- not a failure, just not applicable.
    """
    loaded = _load_bundle_for_content_check(m2_path)
    if loaded is None:
        return None
    result = fidelity_result(loaded[1], read_m2_header_summary(m2_path))
    return None if result is None else _record(m2_path, result)


def test_finite(m2_path):
    loaded = _load_bundle_for_content_check(m2_path)
    if loaded is None:
        return None
    return _record(m2_path, finite_result(*loaded))


def test_mesh_completeness(m2_path):
    loaded = _load_bundle_for_content_check(m2_path)
    if loaded is None:
        return None
    result = mesh_completeness_result(loaded[1])
    return None if result is None else _record(m2_path, result)


# --- validate: production (batchable) / consumption (per-file) split -----

_POINTER_INDEX = re.compile(r"/\d+(?=/|$)")


def parse_validator_output(bundle_dirs, code, out) -> dict:
    """Splits one `husk validate-bundle` run's output into a report per
    bundle dir. Each problem line is `<where>: <message>`, where `<where>`
    is the document's path (as derived from the path husk was given) plus
    `#<json pointer>`; a line is this bundle's when `<where>` lies under its
    directory. A problem under none of them is put in every report's
    `unattributed`, so it fails each file instead of vanishing.
    """
    dirs = [str(d) for d in bundle_dirs]
    reports = {d: {"exit_code": code, "problems": [], "warnings": [], "unattributed": [], "summary": ""}
               for d in dirs}
    summary = ""
    unattributed = []
    for line in out.splitlines():
        if re.match(r"^\d+ documents checked, ", line):
            summary = line
            continue
        where, sep, message = line.partition(": ")
        if not sep:
            continue
        bucket = "problems"
        if message.startswith("warning: "):
            bucket, message = "warnings", message[len("warning: "):]
        entry = {"where": where, "message": message}
        owner = next((d for d in dirs if where == d or where.startswith(d + "/") or where.startswith(d + "#")), None)
        if owner is None:
            if bucket == "problems":
                unattributed.append(entry)
            continue
        reports[owner][bucket].append(entry)
    for report in reports.values():
        report["summary"] = summary
        report["unattributed"] = unattributed
        if not summary:
            report["run_failed"] = _last_meaningful_line(out)
    return reports


def run_bundle_validator(bundle_dirs, timeout=None):
    """Runs `husk validate-bundle` once over every bundle dir in
    `bundle_dirs` (a single path is fine too) and writes each one's
    report to validator_report_path(bundle_dir) -- read that back per file
    via test_validate, which this function deliberately does not do
    itself. Batching is purely a matter of how many dirs one call gets:
    each invocation pays process startup and the embedded schema load
    once, about half a second, so batching many bundles amortizes it.
    Returns (returncode, combined output); a nonzero return means *some*
    bundle had a problem -- read the per-file report via test_validate.
    """
    if isinstance(bundle_dirs, (str, Path)):
        bundle_dirs = [bundle_dirs]
    # Resolved, so the problem paths husk derives (manifest.json and the
    # documents it references, which husk canonicalizes) share the prefix.
    dirs = [Path(d).resolve() for d in bundle_dirs]
    code, out = run_cmd([str(HUSK_BIN), "validate-bundle", *map(str, dirs)],
                        VALIDATOR_TIMEOUT if timeout is None else timeout)
    for d, report in parse_validator_output(dirs, code, out).items():
        validator_report_path(d).write_text(json.dumps(report, indent=2))
    return code, out


def validate_result_from_report(report: dict) -> TestResult:
    """Problems group across a corpus by their JSON pointer with array
    indices folded (`/resources/geometry/mesh/uv_sets/N`); warnings
    (placeholder effects) don't fail outside a release build, matching
    validate-bundle's own exit code.
    """
    if "run_failed" in report:
        return TestResult("validate", [AssertResult(
            "zero_problems", False, code="VALIDATOR_RUN_FAILED", detail=report["run_failed"])])
    problems = report["problems"] + report["unattributed"]
    if not problems:
        return TestResult("validate", [AssertResult("zero_problems", True)])
    first = problems[0]
    pointer = first["where"].partition("#")[2]
    code = "UNATTRIBUTED" if not report["problems"] else (_POINTER_INDEX.sub("/N", pointer) or "DOCUMENT")
    return TestResult("validate", [AssertResult(
        "zero_problems", False, code=code,
        detail=f"{first['where']}: {first['message']}"
               + (f" [+{len(problems) - 1} more]" if len(problems) > 1 else ""))])


def test_validate(m2_path):
    """Consumes whatever report is already sitting next to this file's
    scratch bundle -- produced by a prior run_bundle_validator call,
    targeted at this file alone or as part of a batch. Deliberately does
    not invoke the validator itself: production (batchable) and
    consumption (per-file, idempotent) are separate concerns. Returns None
    if no report exists yet -- not a failure, just not run yet.
    """
    report_path = validator_report_path(scratch_bundle_path(m2_path))
    if not report_path.exists():
        return None
    try:
        report = json.loads(report_path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        return _record(m2_path, TestResult("validate", [AssertResult(
            "zero_problems", False, code="BAD_REPORT", detail=str(e))]))
    return _record(m2_path, validate_result_from_report(report))


def cleanup(m2_path) -> None:
    """Removes this file's scratch bundle dir and validator report (both
    under SCRATCH_DIR) -- everything test_export/run_bundle_validator
    wrote that isn't the status json, which is the actual output and is
    never touched here. Safe to call regardless of whether those exist or
    whether earlier checks even ran.
    """
    bundle_dir = scratch_bundle_path(m2_path)
    validator_report_path(bundle_dir).unlink(missing_ok=True)
    shutil.rmtree(bundle_dir, ignore_errors=True)


# The order to run these in for one file. test_validate is the one
# exception to "fully self-contained": it only finds something once a
# separate run_bundle_validator(bundle_dirs) call -- single-file, or
# batched across many already-exported bundles -- has produced this file's
# report; that call is not itself in this list because it doesn't take a
# single m2_path (see run_bundle_validator's own docstring). Everything
# else needs nothing beyond test_export having run before the three
# bundle-content checks and cleanup.
TEST_ORDER = [
    test_header,
    test_export,
    test_dump_chunks,
    test_fidelity,
    test_finite,
    test_mesh_completeness,
    test_validate,
    cleanup,
]
