#!/usr/bin/env python3
import json
import sys
from pathlib import Path
from collections import defaultdict

try:
    from tqdm import tqdm
except ImportError:
    tqdm = None

ROOT = Path("/media/luna/data/wow_export")
RUN_DIR = Path.cwd()
SUMMARY_PATH = RUN_DIR / "summary.txt"
FAILURES_PATH = RUN_DIR / "failures.txt"
FAILURE_CODES_PATH = RUN_DIR / "failure_codes.txt"
FAILURES_UNIQUE_PATH = RUN_DIR / "failures_unique.txt"


def new_counter():
    return {"success": 0, "failed": 0, "total": 0}


test_counters = defaultdict(new_counter)
assert_counters = defaultdict(lambda: defaultdict(new_counter))
failures = []  # list of (path, {test_name: [failed_assert_dicts]})


def bump(counter: dict, passed: bool):
    counter["total"] += 1
    counter["success" if passed else "failed"] += 1


def process_status_file(status_path: Path):
    try:
        data = json.loads(status_path.read_text())
    except Exception as e:
        print(f"unreadable: {status_path} ({e})", file=sys.stderr)
        return

    path = data.get("path", str(status_path))
    tests = data.get("tests", {})

    local_failed_tests = {}

    for test_name, test_result in tests.items():
        test_passed = bool(test_result.get("passed", False))
        bump(test_counters[test_name], test_passed)

        asserts = test_result.get("asserts", [])
        failed_asserts = []

        for a in asserts:
            a_name = a.get("name", "<unnamed>")
            a_passed = bool(a.get("passed", False))
            bump(assert_counters[test_name][a_name], a_passed)
            if not a_passed:
                failed_asserts.append(a)

        if not test_passed:
            local_failed_tests[test_name] = failed_asserts or [{
                "name": "<test-level>", "passed": False, "code": "", "detail": ""
            }]

    if local_failed_tests:
        failures.append((path, local_failed_tests))


def build_summary_lines():
    lines = ["tests:"]
    for test_name in sorted(test_counters):
        tc = test_counters[test_name]
        lines.append(f"{test_name}: {tc['success']} | {tc['failed']} | {tc['total']}")
        for assert_name in sorted(assert_counters[test_name]):
            ac = assert_counters[test_name][assert_name]
            lines.append(f"     {assert_name}: {ac['success']} | {ac['failed']} | {ac['total']}")
    return lines


def normalize_detail(detail: str, path: str) -> str:
    """Strip the file's own path/dirname/stem out of an error string so
    otherwise-identical failures don't get treated as unique just because
    the message happens to embed the path of the file that triggered it."""
    if not detail:
        return detail
    p = Path(path)
    candidates = sorted({str(p), str(p.parent), p.name, p.stem}, key=len, reverse=True)
    normalized = detail
    for c in candidates:
        if c and c in normalized:
            normalized = normalized.replace(c, "<path>")
    return normalized


def build_failure_outputs():
    if not failures:
        return ["no failures."], ["no failures."], ["no failures."]

    entries = []  # (path, signature)
    signature_count = defaultdict(int)
    signature_example = {}

    for path, failed_tests in failures:
        sig = tuple(
            (test_name, a.get("name"), a.get("code", ""), normalize_detail(a.get("detail", ""), path))
            for test_name, asserts in failed_tests.items()
            for a in asserts
        )
        entries.append((path, sig))
        signature_count[sig] += 1
        signature_example.setdefault(sig, path)

    ranked = sorted(signature_count, key=lambda s: signature_count[s], reverse=True)
    signature_code = {sig: f"FAIL-{i + 1:04d}" for i, sig in enumerate(ranked)}

    failures_lines = []   # repeated-pattern failures: path + code, code detail lives elsewhere
    unique_lines = []     # one-off failures: full detail inline, no shared code to look up

    for path, sig in entries:
        if signature_count[sig] == 1:
            unique_lines.append(f'FAILED: "{path}"')
            for test_name, assert_name, code, detail in sig:
                prefix = f"[{code}] " if code else ""
                unique_lines.append(f'tests.{test_name}.asserts.{assert_name}: {prefix}"{detail}"')
            unique_lines.append("")
        else:
            failures_lines.append(f'FAILED: "{path}" [{signature_code[sig]}]')

    codes_lines = ["failure codes:"]
    had_multi = False
    for sig in ranked:
        if signature_count[sig] == 1:
            continue
        had_multi = True
        codes_lines.append(f"[{signature_code[sig]}] x{signature_count[sig]}  (e.g. {signature_example[sig]})")
        for test_name, assert_name, code, detail in sig:
            prefix = f"[{code}] " if code else ""
            codes_lines.append(f'tests.{test_name}.asserts.{assert_name}: {prefix}"{detail}"')
        codes_lines.append("")

    if not failures_lines:
        failures_lines = ["no repeated-pattern failures -- see failures_unique.txt"]
    if not had_multi:
        codes_lines.append("none -- all failures were one-off, see failures_unique.txt")
    if not unique_lines:
        unique_lines = ["no one-off failures."]

    return failures_lines, codes_lines, unique_lines


def main():
    status_files = list(ROOT.rglob("*.status.json"))
    print(f"found {len(status_files)} status files", file=sys.stderr)

    iterator = status_files
    if tqdm:
        iterator = tqdm(status_files, desc="scanning")

    for status_path in iterator:
        process_status_file(status_path)

    summary_lines = build_summary_lines()
    failures_lines, codes_lines, unique_lines = build_failure_outputs()

    SUMMARY_PATH.write_text("\n".join(summary_lines) + "\n")
    FAILURES_PATH.write_text("\n".join(failures_lines) + "\n")
    FAILURE_CODES_PATH.write_text("\n".join(codes_lines) + "\n")
    FAILURES_UNIQUE_PATH.write_text("\n".join(unique_lines) + "\n")

    print(f"\nwrote {SUMMARY_PATH}")
    print(f"wrote {FAILURES_PATH}")
    print(f"wrote {FAILURE_CODES_PATH}")
    print(f"wrote {FAILURES_UNIQUE_PATH}")
    print(f"failed files: {len(failures)} / {len(status_files)}")


if __name__ == "__main__":
    main()