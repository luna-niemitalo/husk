"""Full-corpus M2 structural validation task, for corpus_scan_framework.py.

Runs, per file: independent header parse cross-check, a *rich* husk export
(model's own directory auto-discovers --skin/--skin-dir/--anim/--skel/
--bones-dir/--phys, --textures deliberately 'none' to keep the mass sweep
fast/disk-bounded -- texture correctness is covered separately by the
Blender render sample, not this pass), dump-chunks,
fidelity/finite/mesh_completeness bundle-content checks, and `husk
validate-bundle` on the bundle. Validation runs per file, unbatched: the
bundle is deleted as soon as the file's checks finish, so the scratch pool
never holds more than one bundle per worker, and a validate run costs a
fraction of the export before it.

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
import corpus_scan_framework as csf  # ROOT read dynamically below, see REFACTOR/CLI_AND_TOOLING.md §4

STATUS_DIR = Path("/media/luna/work/husk_corpus_scratch/status")
SCRATCH_DIR = Path("/media/luna/work/husk_corpus_scratch/scratch")
LIVE_LOG_PATH = Path("/home/luna/dev/husk/corpus_reports/m2_full_validation_live.log")

cc.STATUS_DIR = STATUS_DIR
cc.SCRATCH_DIR = SCRATCH_DIR
cc.TIMEOUT = 60.0

# cc.CORPUS_ROOT is deliberately NOT set here at import time: under
# ProcessPoolExecutor's default fork start method, this module's top-level
# code runs once in the main process (before _init_worker has set
# csf.ROOT), and forked workers just inherit that already-executed,
# stale/unset state -- re-import in the child is a sys.modules cache hit,
# not a re-run. analyze() below sets it fresh on every call instead, which
# is correct under fork, spawn, and thread mode alike.


def _rich_export(m2_path) -> bool:
    """Same shape as corpus_checks.test_export, but with every co-located
    sidecar left at husk's own auto-discovery default (skin/skin-dir/anim/
    skel/bones-dir/phys) instead of test_export's deliberately minimal
    --bones-dir none. Recorded under the 'export' key, same as test_export,
    so the downstream content checks (which read cc.scratch_bundle_path)
    work unmodified.
    """
    bundle_dir = cc.scratch_bundle_path(m2_path)
    result = cc._export_result(m2_path, bundle_dir, ["--anim", "auto", "--textures", "none"])
    return cc._record(m2_path, result)


def _validate(m2_path):
    """None when there is no bundle to validate: test_export already
    recorded why.
    """
    bundle_dir = cc.scratch_bundle_path(m2_path)
    if not (bundle_dir / "manifest.json").exists():
        return None
    cc.run_bundle_validator(bundle_dir)
    return cc.test_validate(m2_path)


GLOB_PATTERNS = ["*.m2"]
FIELDNAMES = ["header_ok", "export_ok", "dump_chunks_ok", "fidelity_ok", "finite_ok",
              "mesh_completeness_ok", "validate_ok", "overall_ok", "first_failure", "first_failure_detail"]
PARALLEL_MODE = "process"
BATCH_SIZE = 4


def analyze(path: Path) -> dict | None:
    cc.CORPUS_ROOT = csf.ROOT
    t0 = time.monotonic()
    header_ok = cc.test_header(path)
    export_ok = _rich_export(path)
    dump_ok = cc.test_dump_chunks(path)
    fidelity = cc.test_fidelity(path)
    finite = cc.test_finite(path)
    mesh_ok = cc.test_mesh_completeness(path)
    validate_ok = _validate(path)

    checks = {
        "header": header_ok, "export": export_ok, "dump_chunks": dump_ok,
        "fidelity": fidelity, "finite": finite, "mesh_completeness": mesh_ok,
        "validate": validate_ok,
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
        "validate_ok": bool(validate_ok) if validate_ok is not None else "n/a",
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
