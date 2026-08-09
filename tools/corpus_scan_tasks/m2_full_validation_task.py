"""Full-corpus M2 structural validation task, for corpus_scan_framework.py.

Runs, per file: independent header parse cross-check, a *rich* husk export
(model's own directory auto-discovers --skin/--skin-dir/--anim/--skel/
--bones-dir/--phys, --textures deliberately 'none' to keep the mass sweep
fast/disk-bounded -- texture embedding + visual correctness is covered
separately by the Blender render sample, not this pass), dump-chunks,
fidelity/finite/mesh_completeness glb-content checks. gltf_validator is
deliberately NOT run here (Dart VM per-invocation startup cost across
130k+ files, and keeping every glb on disk long enough to batch it risks
filling the scratch pool) -- it runs against the smaller Blender-sample
set instead, where full textures are also embedded.

Every check is a silent pass (just a CSV row + copied status.json); any
failing check appends one line to LIVE_LOG_PATH immediately, so a human
can `tail -f` it during the run instead of waiting for the final report.
"""

from __future__ import annotations

import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import corpus_checks as cc

CORPUS_ROOT = Path("/media/luna/data/wow_export")
STATUS_DIR = Path("/media/luna/work/husk_corpus_scratch/status")
SCRATCH_DIR = Path("/media/luna/work/husk_corpus_scratch/scratch")
LIVE_LOG_PATH = Path("/home/luna/dev/husk/corpus_reports/m2_full_validation_live.log")

cc.CORPUS_ROOT = CORPUS_ROOT
cc.STATUS_DIR = STATUS_DIR
cc.SCRATCH_DIR = SCRATCH_DIR
cc.HUSK_BIN = Path("/home/luna/dev/husk/build/husk")
cc.TIMEOUT = 60.0


def _rich_export(m2_path) -> bool:
    """Same shape as corpus_checks.test_export, but with every co-located
    sidecar left at husk's own auto-discovery default (skin/skin-dir/anim/
    skel/bones-dir/phys) plus --collision, instead of test_export's
    deliberately minimal --textures none/--bones-dir none/no --collision.
    Recorded under the 'export' key, same as test_export would use, so
    downstream fidelity/finite/mesh_completeness (which just read
    cc.scratch_glb_path) work unmodified.
    """
    out_glb = cc.scratch_glb_path(m2_path)
    out_glb.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(cc.HUSK_BIN), "export", str(m2_path), "-o", str(out_glb),
           "--anim", "auto", "--textures", "none", "--collision"]
    code, out = cc.run_cmd(cmd, cc.TIMEOUT)
    exists = out_glb.exists()
    size_ok = exists and out_glb.stat().st_size > 12
    magic_ok = False
    if size_ok:
        with open(out_glb, "rb") as f:
            magic_ok = f.read(4) == b"glTF"
    asserts = [
        cc.AssertResult("exit_code_0", code == 0, detail=cc._last_meaningful_line(out) if code != 0 else ""),
        cc.AssertResult("glb_written", exists, detail="" if exists else "no output file"),
        cc.AssertResult("glb_nonempty", size_ok, detail="" if size_ok else "empty or missing"),
        cc.AssertResult("glb_magic", magic_ok, detail="" if magic_ok else "bad glTF magic bytes"),
    ]
    return cc._record(m2_path, cc.TestResult("export", asserts))


def _fixed_fidelity(m2_path):
    """corpus_checks.test_fidelity's bone_count_matches_header assert
    predates the geoset-tag-joint feature (CLAUDE.md's Resume: "every
    export also emitting one inert geoset 'tag' joint per distinct geoset
    ID ... appended to the skin strictly after every real bone") -- a real
    export now legitimately has skin.joints == header bone_count + (number
    of distinct geoset-tag nodes), not an equality. Confirmed against a
    real corpus file (test/number_04.m2: header says 1 bone, glb skin has
    joints [bone_0, group_0,variant_0] -- 2 total, exactly 1 real bone + 1
    tag joint) before writing this fix, not assumed. Tag joints are
    identified structurally by node name prefix 'group_' (gltf_skeleton.cpp's
    own literal), not by guessing a count.
    """
    loaded = cc._load_glb_for_content_check(m2_path)
    header_summary = cc.read_m2_header_summary(m2_path)
    if loaded is None or header_summary is None:
        return None
    gltf, _bin = loaded
    asserts = []
    meshes = gltf.get("meshes", [])
    accessors = gltf.get("accessors", [])
    excluded = cc._collision_mesh_indices(gltf)
    position_counts = {
        accessors[prim["attributes"]["POSITION"]]["count"]
        for mi, mesh in enumerate(meshes) if mi not in excluded
        for prim in mesh.get("primitives", [])
        if "POSITION" in prim.get("attributes", {})
    }
    if position_counts:
        consistent = len(position_counts) == 1
        asserts.append(cc.AssertResult("one_consistent_vertex_count", consistent,
                                        detail="" if consistent else f"primitives disagree on vertex count: {position_counts}"))
        if consistent:
            vertex_count = next(iter(position_counts))
            asserts.append(cc.AssertResult(
                "vertex_count_matches_header", vertex_count == header_summary["vertex_count"],
                detail="" if vertex_count == header_summary["vertex_count"]
                else f"glb has {vertex_count}, header says {header_summary['vertex_count']}"))

    skins = gltf.get("skins", [])
    if skins and header_summary["bone_count"] > 0:
        nodes = gltf.get("nodes", [])
        joint_indices = skins[0].get("joints", [])
        tag_joint_count = sum(1 for ji in joint_indices if str(nodes[ji].get("name", "")).startswith("group_"))
        real_joint_count = len(joint_indices) - tag_joint_count
        asserts.append(cc.AssertResult(
            "bone_count_matches_header_excl_tag_joints", real_joint_count == header_summary["bone_count"],
            detail="" if real_joint_count == header_summary["bone_count"]
            else (f"glb skin has {len(joint_indices)} joints ({tag_joint_count} geoset-tag), "
                  f"{real_joint_count} real, header says {header_summary['bone_count']} bones")))

    if not asserts:
        return None
    return cc._record(m2_path, cc.TestResult("fidelity", asserts))


GLOB_PATTERNS = ["*.m2"]
FIELDNAMES = ["header_ok", "export_ok", "dump_chunks_ok", "fidelity_ok", "finite_ok",
              "mesh_completeness_ok", "overall_ok", "first_failure", "first_failure_detail"]
PARALLEL_MODE = "process"
BATCH_SIZE = 4


def analyze(path: Path) -> dict | None:
    t0 = time.monotonic()
    header_ok = cc.test_header(path)
    export_ok = _rich_export(path)
    dump_ok = cc.test_dump_chunks(path)
    fidelity = _fixed_fidelity(path)
    finite = cc.test_finite(path)
    mesh_ok = cc.test_mesh_completeness(path)

    checks = {
        "header": header_ok, "export": export_ok, "dump_chunks": dump_ok,
        "fidelity": fidelity, "finite": finite, "mesh_completeness": mesh_ok,
    }
    overall_ok = all(v is not False for v in checks.values())

    # Established convention (see corpus_checks_example.py): mirror the
    # status json right beside the .m2 file itself in the corpus, so it's
    # human-browsable in place, not just in the aggregate CSV.
    try:
        shutil.copyfile(cc.status_path(path), str(path) + ".status.json")
    except OSError:
        pass

    first_failure, first_detail = "", ""
    if not overall_ok:
        status = cc._load_status(path)
        for name, result in status.get("tests", {}).items():
            for a in result.get("asserts", []):
                if not a["passed"]:
                    first_failure = f"{name}.{a['name']}"
                    first_detail = a["detail"] or a["code"]
                    break
            if first_failure:
                break
        with LIVE_LOG_PATH.open("a") as f:
            f.write(f"FAIL {path} :: {first_failure} :: {first_detail}\n")

    cc.cleanup(path)
    elapsed = time.monotonic() - t0
    return {
        "header_ok": header_ok, "export_ok": export_ok, "dump_chunks_ok": dump_ok,
        "fidelity_ok": bool(fidelity) if fidelity is not None else "n/a",
        "finite_ok": bool(finite) if finite is not None else "n/a",
        "mesh_completeness_ok": bool(mesh_ok) if mesh_ok is not None else "n/a",
        "overall_ok": overall_ok, "first_failure": first_failure, "first_failure_detail": first_detail[:300],
    }


def summarize(rows: list[dict], total_files: int) -> list[str]:
    passed = sum(1 for r in rows if r["overall_ok"])
    failed = total_files - passed
    lines = [f"{passed}/{total_files} files fully passed ({passed/total_files:.2%})",
             f"{failed}/{total_files} files had at least one failing check"]
    from collections import Counter
    cats = Counter(r["first_failure"] for r in rows if not r["overall_ok"])
    lines.append("failure breakdown by first-failing check:")
    for check, n in cats.most_common(30):
        lines.append(f"  {n:6d}  {check}")
    return lines


class M2FullValidationTask:
    """Class wrapper so corpus_scan_framework's 'module:AttrName' --task
    spec has a real attribute to getattr() -- the framework only ever
    calls these as functions (never instantiates), so free module-level
    functions above stay the actual implementation.
    """
    GLOB_PATTERNS = GLOB_PATTERNS
    FIELDNAMES = FIELDNAMES
    PARALLEL_MODE = PARALLEL_MODE
    BATCH_SIZE = BATCH_SIZE
    analyze = staticmethod(analyze)
    summarize = staticmethod(summarize)
