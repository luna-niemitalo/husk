#!/usr/bin/env python3
"""Verifies the appearance-string pipeline end to end, one step at a time:

  synthetic Blizzard equipment/appearance JSON
    -> tools/blizzard_profile_to_appearance_string.py
    -> husk appearance-string --validate

Every step prints its own expected-vs-got line and PASS/FAIL before moving
on, instead of trusting a chain of shell commands to fail loudly on its own
(CLAUDE.md's Foreign Data policy: "on failure, always print expected and
actual values" -- this script is that policy applied to the pipeline itself,
not just to husk's own parser).

Run with the project's own tooling venv, not a bare `python3` (this repo has
no system-wide Python install -- see nix/flake.nix):

    tools/venv/bin/python3 tools/verify_appearance_string_pipeline.py
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONVERTER = REPO_ROOT / "tools" / "blizzard_profile_to_appearance_string.py"
HUSK_BIN = REPO_ROOT / "build" / "husk"

SYNTHETIC_EQUIPMENT = {
    "equipped_items": [
        {"slot": {"type": "HEAD"}, "item": {"id": 1}, "transmog": {"item_modified_appearance_id": 236345}},
        {"slot": {"type": "CHEST"}, "item": {"id": 2}, "modified_appearance_id": 227936},
        {"slot": {"type": "FINGER_1"}, "item": {"id": 3}, "modified_appearance_id": 0},
    ]
}
SYNTHETIC_APPEARANCE = {
    "customizations": [{"choice": {"id": 8909}}, {"choice": {"id": 2115}}]
}
EXPECTED_STRING = "husk-appearance/1 race=52 sex=0 cust=2115,8909 gear=CHEST:227936,HEAD:236345"

failures = 0


def report(step: str, expected: str, got: str) -> bool:
    global failures
    ok = expected == got
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {step}")
    print(f"    expected: {expected!r}")
    print(f"    got:      {got!r}")
    if not ok:
        failures += 1
    return ok


def run(cmd: list) -> tuple[int, str, str]:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc.returncode, proc.stdout.strip(), proc.stderr.strip()


def main() -> int:
    if not HUSK_BIN.exists():
        print(f"husk binary not found at {HUSK_BIN} -- build it first: cmake --build build", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        eq_path = Path(tmp) / "equipment.json"
        app_path = Path(tmp) / "appearance.json"
        eq_path.write_text(json.dumps(SYNTHETIC_EQUIPMENT))
        app_path.write_text(json.dumps(SYNTHETIC_APPEARANCE))

        # Step 1: converter script produces the expected husk-appearance string.
        code, out, err = run([sys.executable, str(CONVERTER), "--race", "52", "--sex", "0",
                               str(eq_path), str(app_path)])
        if code != 0:
            report("converter exits 0", "exit 0", f"exit {code} (stderr: {err})")
        else:
            report("converter output", EXPECTED_STRING, out)

        # Step 2: husk itself accepts and canonicalizes that exact string.
        code, out, err = run([str(HUSK_BIN), "appearance-string", "--validate", out or EXPECTED_STRING])
        if code != 0:
            report("husk validate exits 0", "exit 0", f"exit {code} (stderr: {err})")
        else:
            canonical_line = next((l for l in out.splitlines() if l.startswith("husk: appearance-string: canonical:")), "")
            got_canonical = canonical_line.removeprefix("husk: appearance-string: canonical: ")
            report("husk canonical form", EXPECTED_STRING, got_canonical)

    print()
    if failures:
        print(f"{failures} step(s) failed -- see expected/got above.")
        return 1
    print("all steps passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
