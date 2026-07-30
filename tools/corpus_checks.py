#!/usr/bin/env python3
"""Library of idempotent, per-file M2 corpus checks.

Extracted from corpus_test.py (kept as-is, untouched, as a reference for
its own now-superseded threading/tqdm/batching approach -- this file
replaces that approach, not that file). Every public check function below
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
gltf_validator report (run_gltf_validator) is a batchable, orchestration-
level concern -- gltf_validator's own per-invocation Dart VM startup cost
makes calling it once per file dramatically slower than pointing it at a
directory of many already-exported files at once (see run_gltf_validator's
own docstring) -- while consuming that report into a file's status JSON
(test_validate) stays a plain, idempotent, single-file-path check like
everything else. Building the batching itself is the caller's call.
"""

import json
import math
import shutil
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# --- caller-configured globals ------------------------------------------
# Set these once, before calling any check. A scheduler driving many
# worker threads/processes can share one import of this module read-only
# after that -- nothing here is ever written per-file.
CORPUS_ROOT: Path | None = None            # root every mirrored path is relative to
STATUS_DIR: Path | None = None             # per-file <name>.status.json, mirrored under here
SCRATCH_DIR: Path | None = None            # per-file scratch .glb + validator report, mirrored (put this on tmpfs)
HUSK_BIN = REPO_ROOT / "build" / "husk"
GLTF_VALIDATOR_BIN = shutil.which("gltf_validator")
TIMEOUT = 30.0                             # seconds, per husk invocation
VALIDATOR_TIMEOUT = 120.0                  # seconds, per gltf_validator invocation
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


def scratch_glb_path(m2_path) -> Path:
    rel = _relative(m2_path)
    return SCRATCH_DIR / rel.parent / (rel.name + ".glb")


def _report_path(glb_path: Path) -> Path:
    # gltf_validator's own naming convention (single-file or directory mode alike).
    return glb_path.with_name(glb_path.name + ".report.json")


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
            return line.strip()[:400]
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
                                     detail="" if "version:" in out else out[:400]))
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


def test_export(m2_path) -> bool:
    out_glb = scratch_glb_path(m2_path)
    out_glb.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(HUSK_BIN), "export", str(m2_path), "-o", str(out_glb),
        "--textures", "none", "--anim", ANIM_MODE, "--bones-dir", "none",
    ]
    code, out = run_cmd(cmd, TIMEOUT)
    exists = out_glb.exists()
    size_ok = exists and out_glb.stat().st_size > 12
    magic_ok = False
    if size_ok:
        with open(out_glb, "rb") as f:
            magic_ok = f.read(4) == b"glTF"
    asserts = [
        AssertResult("exit_code_0", code == 0, detail=_last_meaningful_line(out) if code != 0 else ""),
        AssertResult("glb_written", exists, detail="" if exists else "no output file"),
        AssertResult("glb_nonempty", size_ok, detail="" if size_ok else "empty or missing"),
        AssertResult("glb_magic", magic_ok, detail="" if magic_ok else "bad glTF magic bytes"),
    ]
    return _record(m2_path, TestResult("export", asserts))


def test_dump_chunks(m2_path) -> bool:
    """Runs unconditionally on every file, chunked or not: src/cmd_dump.cpp's
    `header.chunked` early-return only gates the Legion+ chunk-tag section
    now -- ribbon/particle-emitter JSON is emitted for every M2 version
    (see CLAUDE.md's Resume) -- so there's no format this can be N/A for.
    """
    code, out = run_cmd([str(HUSK_BIN), "dump-chunks", str(m2_path)], TIMEOUT)
    looks_json = out.strip().startswith("{")
    asserts = [
        AssertResult("exit_code_0", code == 0, detail=_last_meaningful_line(out) if code != 0 else ""),
        AssertResult("looks_like_json", looks_json, detail="" if looks_json else out[:200]),
    ]
    return _record(m2_path, TestResult("dump-chunks", asserts))


# --- glb content checks (fidelity/finite/mesh_completeness) --------------
# A minimal, from-scratch glb reader -- deliberately not a dependency like
# pygltflib, since all that's needed is "read this accessor's raw values,"
# and every glb husk produces uses plain (non-sparse), single-bufferView
# accessors. Sparse accessors are recognized and skipped (husk doesn't emit
# them; a skip here just means "not checked," never a silent misread).

_COMPONENT_TYPES = {
    5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
    5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4),
}
_TYPE_COMPONENT_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}


def _read_glb(glb_path: Path):
    data = glb_path.read_bytes()
    magic, _version, length = struct.unpack_from("<4sII", data, 0)
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


def _read_accessor_values(gltf, bin_chunk, accessor_index):
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


def _collision_mesh_indices(gltf) -> set:
    # The collision mesh (if present) is a genuinely separate NamedMesh with
    # its own, unrelated, much smaller vertex buffer and no UV data at all
    # -- tagged via {"collision": true} on its *node*. fidelity (vertex-count
    # consistency) and mesh_completeness (UV presence) both exclude it.
    return {
        node["mesh"] for node in gltf.get("nodes", [])
        if node.get("extras", {}).get("collision") is True and "mesh" in node
    }


def _load_glb_for_content_check(m2_path):
    """Shared by test_fidelity/test_finite/test_mesh_completeness -- each
    still takes only m2_path and is independently callable, this just
    avoids parsing the same glb three times when all three run back to
    back, the normal case (third real occurrence of this exact read --
    this codebase's own bar for a shared helper). Returns (gltf, bin_chunk),
    or None if the glb isn't there yet (test_export hasn't run) or doesn't
    parse.
    """
    glb_path = scratch_glb_path(m2_path)
    if not glb_path.exists():
        return None
    try:
        return _read_glb(glb_path)
    except (ValueError, KeyError, struct.error, json.JSONDecodeError):
        return None


def test_fidelity(m2_path):
    """Cross-checks husk's exported glTF against an *independently* parsed
    M2 header -- not a hardcoded expected value, so this generalizes to any
    file. Exact match, no tolerance (same precedent as
    tests/test_conformance.cpp's case-1/case-2 checks). Returns None (no
    status entry written) when the glb or the independent parse isn't
    available yet -- not a failure, just not applicable yet.
    """
    loaded = _load_glb_for_content_check(m2_path)
    header_summary = read_m2_header_summary(m2_path)
    if loaded is None or header_summary is None:
        return None
    gltf, _bin_chunk = loaded
    asserts = []
    meshes = gltf.get("meshes", [])
    accessors = gltf.get("accessors", [])
    excluded = _collision_mesh_indices(gltf)
    position_counts = {
        accessors[prim["attributes"]["POSITION"]]["count"]
        for mi, mesh in enumerate(meshes) if mi not in excluded
        for prim in mesh.get("primitives", [])
        if "POSITION" in prim.get("attributes", {})
    }
    if position_counts:
        # All primitives share one vertex buffer in husk's own design, so
        # every primitive's POSITION accessor should report the same count.
        consistent = len(position_counts) == 1
        asserts.append(AssertResult("one_consistent_vertex_count", consistent,
                                     detail="" if consistent
                                     else f"primitives disagree on vertex count: {position_counts}"))
        if consistent:
            vertex_count = next(iter(position_counts))
            asserts.append(AssertResult(
                "vertex_count_matches_header", vertex_count == header_summary["vertex_count"],
                detail="" if vertex_count == header_summary["vertex_count"]
                else f"glb has {vertex_count}, header says {header_summary['vertex_count']}"))

    # header_summary["bone_count"] == 0 legitimately means bones live in an
    # external .skel file, not inline -- this independent reader doesn't
    # parse .skel, so only assert when the M2 itself claims inline bones.
    skins = gltf.get("skins", [])
    if skins and header_summary["bone_count"] > 0:
        joint_count = len(skins[0].get("joints", []))
        asserts.append(AssertResult(
            "bone_count_matches_header", joint_count == header_summary["bone_count"],
            detail="" if joint_count == header_summary["bone_count"]
            else f"glb skin has {joint_count} joints, header says {header_summary['bone_count']} bones"))

    if not asserts:
        return None
    return _record(m2_path, TestResult("fidelity", asserts))


def test_finite(m2_path):
    """Sweeps every FLOAT accessor -- vertex positions/normals/UVs *and*
    every animation sampler's keyframe input/output -- for NaN/Inf. A
    static check on stored data, not a full animation-playback simulation,
    but a keyframe that's already non-finite at rest will definitely
    "explode" the moment anything interpolates it.
    """
    loaded = _load_glb_for_content_check(m2_path)
    if loaded is None:
        return None
    gltf, bin_chunk = loaded
    bad = []
    for i, acc in enumerate(gltf.get("accessors", [])):
        if acc.get("componentType") != 5126:  # FLOAT only
            continue
        values = _read_accessor_values(gltf, bin_chunk, i)
        if values is None:
            continue
        if not all(math.isfinite(v) for v in values):
            bad.append(i)
    passed = not bad
    return _record(m2_path, TestResult("finite", [AssertResult(
        "all_float_accessors_finite", passed,
        detail="" if passed else f"non-finite values in accessor(s) {bad[:10]}")]))


def test_mesh_completeness(m2_path):
    """Does every primitive actually carry UV data, not just claim to via a
    material reference. husk always exports UVs when the M2 vertex format
    has them (effectively always for real character/creature/item models),
    so a primitive missing TEXCOORD_0 is worth a look, not expected noise.
    """
    loaded = _load_glb_for_content_check(m2_path)
    if loaded is None:
        return None
    gltf, _bin_chunk = loaded
    excluded = _collision_mesh_indices(gltf)  # never textured/UV-mapped by design
    missing = []
    for mi, mesh in enumerate(gltf.get("meshes", [])):
        if mi in excluded:
            continue
        for pi, prim in enumerate(mesh.get("primitives", [])):
            if "TEXCOORD_0" not in prim.get("attributes", {}):
                missing.append(f"mesh{mi}/prim{pi}")
    passed = not missing
    return _record(m2_path, TestResult("mesh_completeness", [AssertResult(
        "every_primitive_has_texcoord0", passed,
        detail="" if passed else f"missing on {missing[:10]}")]))


# --- validate: production (batchable) / consumption (per-file) split -----

def run_gltf_validator(target, timeout=None):
    """Runs the real gltf_validator CLI against `target`, which may be a
    single .glb file or a directory -- gltf_validator recurses through a
    directory itself, one Dart VM startup shared across every asset inside
    (see `gltf_validator --help`). That's the entire reason pointing this
    at a directory of many already-exported glbs is dramatically faster,
    per file, than calling it once per file: batching here is purely a
    matter of which `target` you pass in, not a separate code path.
    Writes `<glb>.report.json` next to each asset it touches -- read that
    back per file via test_validate, which this function deliberately does
    not do itself. Returns (returncode, combined stdout+stderr); a nonzero
    return means *some* asset under target had an error, not necessarily
    the one you care about if target was a directory -- read the per-file
    report via test_validate, don't trust this return value alone.
    """
    if GLTF_VALIDATOR_BIN is None:
        raise RuntimeError("GLTF_VALIDATOR_BIN not set / gltf_validator not found on PATH")
    return run_cmd([str(GLTF_VALIDATOR_BIN), str(target)], VALIDATOR_TIMEOUT if timeout is None else timeout)


def test_validate(m2_path):
    """Consumes whatever `<glb>.report.json` is already sitting next to
    this file's scratch glb -- produced by a prior run_gltf_validator call,
    targeted at this file alone or as part of a directory-wide batch (see
    run_gltf_validator). Deliberately does not invoke the validator itself:
    production (batchable) and consumption (per-file, idempotent) are
    separate concerns. Returns None if no report exists yet -- not a
    failure, just not run yet.
    """
    report_path = _report_path(scratch_glb_path(m2_path))
    if not report_path.exists():
        return None
    try:
        report = json.loads(report_path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        return _record(m2_path, TestResult("validate", [AssertResult(
            "zero_errors", False, code="BAD_REPORT", detail=str(e))]))
    issues = report.get("issues", {})
    errors = [m for m in issues.get("messages", []) if m.get("severity") == 0]
    if not errors:
        return _record(m2_path, TestResult("validate", [AssertResult("zero_errors", True)]))
    first = errors[0]
    return _record(m2_path, TestResult("validate", [AssertResult(
        "zero_errors", False, code=first.get("code", "?"),
        detail=f"{first.get('code')}: {first.get('message')} ({first.get('pointer')})"
               + (f" [+{len(errors) - 1} more]" if len(errors) > 1 else ""))]))


def cleanup(m2_path) -> None:
    """Removes this file's scratch .glb and validator report sidecar (both
    under SCRATCH_DIR) -- everything test_export/run_gltf_validator wrote
    that isn't the status json, which is the actual output and is never
    touched here. Safe to call regardless of whether those files exist or
    whether earlier checks even ran.
    """
    glb_path = scratch_glb_path(m2_path)
    _report_path(glb_path).unlink(missing_ok=True)
    glb_path.unlink(missing_ok=True)


# The order to run these in for one file. test_validate is the one
# exception to "fully self-contained": it only finds something once a
# separate run_gltf_validator(target) call -- single-file, or (much faster)
# batched across many already-exported glbs -- has produced a report.json
# for this file's scratch glb; that call is not itself in this list because
# it doesn't take a single m2_path (see run_gltf_validator's own
# docstring). Everything else needs nothing beyond test_export having run
# before the three glb-content checks and cleanup.
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
