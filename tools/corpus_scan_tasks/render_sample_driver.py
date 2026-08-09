#!/usr/bin/env python3
"""Drives the end-to-end husk export -> headless Blender render pipeline
over a file list (one .m2 absolute path per line), in parallel across
processes (each unit is two subprocess launches: husk, then blender --
safe to run many at once, no shared state). For every file:

  1. `husk export` with full auto-discovered sidecars (textures/skin/anim/
     skel/bones/phys all default-resolved from the model's own directory)
     -> a scratch .glb.
  2. `blender --background` running render_glb.py against that .glb ->
     one PNG under RENDER_DIR, mirroring the corpus's own relative path.
  3. The scratch .glb is deleted immediately after the render attempt,
     succeed or fail -- never accumulated (per-file peak, not corpus-wide).

Every result (pass or fail) is appended as one line to LIVE_LOG (visible,
tailable) the instant it's known -- a silent 'OK' line on success, a full
husk-and/or-Blender stderr excerpt on failure, so a human can watch this
run live instead of waiting for the final summary. A machine-readable
CSV is also written for the final aggregate report.

Concurrency is live-tuned by corpus_scan_framework.py's own
AdaptiveConcurrency (same TCP-AIMD-style controller that module's docstring
describes), not a fixed number applied for the whole run -- --max-workers
is a ceiling to ramp up toward, not the count used throughout. Chosen after
finding this pipeline's own real per-file cost is heavily heavy-tailed (a
plain item renders in ~2s, a complex character in 50-90s), the same shape
that controller was built for.

Usage:
    direnv exec . tools/venv/bin/python tools/corpus_scan_tasks/render_sample_driver.py \\
        corpus_reports/render_sample.txt corpus_reports/renders --max-workers 16
"""
from __future__ import annotations

import csv
import json
import os
import subprocess
import sys
import time
from concurrent.futures import FIRST_COMPLETED, Future, ProcessPoolExecutor, wait
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from corpus_scan_framework import AdaptiveConcurrency  # noqa: E402 -- see sys.path.insert above

CORPUS_ROOT = Path("/media/luna/data/wow_export")
HUSK_BIN = Path("/home/luna/dev/husk/build/husk")
BLENDER_BIN = "blender"
RENDER_SCRIPT = Path(__file__).resolve().parent / "render_glb.py"
SCRATCH_DIR = Path("/media/luna/work/husk_corpus_scratch/render_glbs")
EXPORT_TIMEOUT = 120.0
RENDER_TIMEOUT = 180.0

# fox_nest-specific (this machine's own dual Radeon VII, CLAUDE.md's own
# Machines table) -- 12 parallel workers were all serializing their
# render_glb.py call onto whichever single GPU Blender's OpenGL backend
# defaults to, real, observed as "GPU used variably, CPU only 50-60%" (one
# GPU idle) rather than either resource actually saturating. DRI_PRIME
# (PRIME render-offload, the OpenGL/EGL selection mechanism -- confirmed by
# empirically toggling it and watching /sys/class/drm/card*/device/
# gpu_busy_percent light up on the intended card, not guessed) alternates
# each worker process across both cards, so render work actually overlaps
# instead of queueing on one device. Vulkan (--gpu-backend vulkan) was also
# tried: no measurable per-render speedup on this workload (these are
# simple flat-shaded 640x480 thumbnails, likely startup/CPU-bound rather
# than GPU-bound), and Mesa's own multi-GPU device-select env var
# (MESA_VK_DEVICE_SELECT) didn't behave deterministically in testing -- not
# used here for that reason, staying on the OpenGL default plus DRI_PRIME.
GPU_PCI_BUS_IDS = ["pci-0000_10_00_0", "pci-0000_0d_00_0"]

# --listfile-root is deliberately CORPUS_ROOT, never the (unset, so
# model's-own-directory) --textures value below -- husk's own
# --listfile-root doc comment/DESIGN.md explain why: --textures drives a
# flat, non-recursive directory scan for the pre-existing embedded-name/
# same-basename matching, which would go blind across the whole corpus if
# pointed at CORPUS_ROOT just to make --listfile resolve. Real listfile
# path, gitignored per this project's own "user-populated, local dev
# machine only" convention for large downloaded corpus artifacts -- not
# committed, not fetched by this script.
#
# The full 148MB/2.2M-line community-listfile.csv, deliberately not a
# filtered subset -- an earlier version of this pruned it down to only the
# FileDataIDs a specific corpus scan had already flagged, to work around
# `src/listfile.cpp`'s own re-parse-per-invocation cost (husk has no
# persistent process to cache it across this driver's 130,576 `husk
# export` calls). That pruning was reverted: it risked silently missing
# coverage for any FileDataID not already flagged by that one snapshot
# scan. Fixed at the real root instead -- `loadListfile` was rewritten for
# raw parse speed (single fread + manual scanning, reserved hashmap, no
# per-line exceptions) -- confirmed by direct timing to bring the full
# file's per-call overhead down to noise level (~4.4s with --listfile vs
# ~4.5s without, on a real fixture), so every real entry is available with
# no pruning risk and no meaningful cost.
LISTFILE = Path("/media/luna/userdata/Downloads/community-listfile.csv")


def _last_lines(text: str, n: int = 6) -> str:
    lines = [l for l in text.splitlines() if l.strip()]
    return " | ".join(lines[-n:])


def process_one(m2_path_str: str, render_dir_str: str, live_log_str: str) -> dict:
    m2_path = Path(m2_path_str)
    render_dir = Path(render_dir_str)
    live_log = Path(live_log_str)
    rel = m2_path.resolve().relative_to(CORPUS_ROOT)
    scratch_glb = SCRATCH_DIR / rel.parent / (rel.name + ".glb")
    # New renders write .webp (render_glb.py switched from PNG, lighter for
    # the live gallery); a .png left over from before that switch, or from
    # before --listfile existed, still counts as "already done" here -- only
    # files whose PNG was deliberately deleted (the missing-texture-gap
    # re-render pass) or never rendered at all get redone, and they land as
    # .webp. The two formats coexist fine: tools/live_gallery_server.py
    # already recognizes both extensions.
    out_webp = render_dir / rel.parent / (rel.name + ".webp")
    out_png_legacy = render_dir / rel.parent / (rel.name + ".png")
    scratch_glb.parent.mkdir(parents=True, exist_ok=True)
    out_webp.parent.mkdir(parents=True, exist_ok=True)

    if out_webp.exists() or out_png_legacy.exists():
        # Resume support for the full 130k-file run: this job is expected to
        # take hours, so a restart (crash, machine reboot) must not re-do
        # already-rendered files. A prior image is treated as done -- this
        # driver never writes a partial/temp file directly at the output
        # path (Blender's own render.render(write_still=True) is atomic-
        # enough for this purpose: it writes the final file directly, but
        # only after a successful render call returns).
        return {"path": str(m2_path), "export_ok": True, "render_ok": True,
                "skipped_no_geometry": False, "detail": "resumed: already rendered"}

    row = {"path": str(m2_path), "export_ok": False, "render_ok": False, "skipped_no_geometry": False, "detail": ""}
    t0 = time.monotonic()
    try:
        cmd = [str(HUSK_BIN), "export", str(m2_path), "-o", str(scratch_glb), "--anim", "auto"]
        if LISTFILE.exists():
            cmd += ["--listfile", str(LISTFILE), "--listfile-root", str(CORPUS_ROOT)]
        p = subprocess.run(
            cmd,
            capture_output=True, text=True, timeout=EXPORT_TIMEOUT,
        )
        if p.returncode != 0 or not scratch_glb.exists():
            row["detail"] = f"export failed: {_last_lines(p.stdout + p.stderr)}"
            _log(live_log, "FAIL-EXPORT", m2_path, row["detail"])
            return row
        row["export_ok"] = True
    except subprocess.TimeoutExpired:
        row["detail"] = f"export TIMEOUT after {EXPORT_TIMEOUT}s"
        _log(live_log, "FAIL-EXPORT", m2_path, row["detail"])
        return row

    try:
        # Stable per-worker-process GPU assignment, not per-file -- a
        # ProcessPoolExecutor worker is long-lived and handles many files,
        # so pinning by pid (fixed for that worker's whole lifetime) spreads
        # the pool's workers across both cards without any shared state or
        # locking between them.
        env = dict(os.environ)
        env["DRI_PRIME"] = GPU_PCI_BUS_IDS[os.getpid() % len(GPU_PCI_BUS_IDS)]
        p = subprocess.run(
            [BLENDER_BIN, "--background", "--factory-startup", "--python", str(RENDER_SCRIPT),
             "--", str(scratch_glb), str(out_webp)],
            capture_output=True, text=True, timeout=RENDER_TIMEOUT, env=env,
        )
        if p.returncode == 0 and "SKIPPED no mesh objects" in p.stdout:
            row["skipped_no_geometry"] = True
            _log(live_log, "SKIP", m2_path, "0-vertex model (camera/track-only), nothing to render")
        elif p.returncode != 0 or not out_webp.exists():
            row["detail"] = f"render failed: {_last_lines(p.stdout + p.stderr)}"
            _log(live_log, "FAIL-RENDER", m2_path, row["detail"])
        else:
            row["render_ok"] = True
            _log(live_log, "OK", m2_path, f"{time.monotonic()-t0:.1f}s -> {out_webp}")
    except subprocess.TimeoutExpired:
        row["detail"] = f"render TIMEOUT after {RENDER_TIMEOUT}s"
        _log(live_log, "FAIL-RENDER", m2_path, row["detail"])
    finally:
        scratch_glb.unlink(missing_ok=True)

    return row


def _log(live_log: Path, status: str, m2_path: Path, detail: str) -> None:
    with live_log.open("a") as f:
        f.write(f"{status} {m2_path} :: {detail}\n")


def _write_stats(stats_path: Path, **fields) -> None:
    # A separate small JSON file, not just stdout -- the periodic "N done"
    # print lines land in a log Python block-buffers whenever stdout isn't
    # a real terminal (redirected to a file, as this driver always is when
    # backgrounded), which made a real, healthy run look stalled for
    # minutes at a time in practice. This file is written with a real
    # os.replace (atomic on the same filesystem), so a reader never sees a
    # half-written file -- tools/live_gallery_server.py polls it directly
    # for the concurrency controller's own internals (window/backoff/rate),
    # which aren't otherwise observable from outside this process.
    tmp = stats_path.with_suffix(".tmp")
    tmp.write_text(json.dumps({"updated": time.time(), **fields}))
    tmp.replace(stats_path)


def main() -> int:
    # Line-buffer stdout even when redirected to a file (the normal case
    # for a backgrounded multi-hour run) -- block-buffering otherwise
    # delays every "N done" print by minutes, which made a real, healthy
    # run look stalled when checked against the log file directly (real
    # progress was only visible by independently counting output images on
    # disk). reconfigure() is a real stdlib method (Python 3.7+), not a
    # hack -- equivalent to `python -u` but from inside the script itself,
    # so callers don't have to remember the flag.
    sys.stdout.reconfigure(line_buffering=True)

    file_list = Path(sys.argv[1])
    render_dir = Path(sys.argv[2])
    # --max-workers is a *ceiling* the controller ramps up toward, not a
    # fixed count applied for the whole run -- this pipeline's own per-file
    # cost is heavily heavy-tailed (a plain item renders in ~2s, a complex
    # character in 50-90s, confirmed in the live log), exactly the shape
    # corpus_scan_framework.py's own AdaptiveConcurrency docstring describes
    # itself as built for. A fixed guess either leaves throughput on the
    # table (too low) or thrashes CPU/GPU without any way to notice (too
    # high, and this pipeline has no other feedback signal short of a human
    # watching gpu_busy_percent by hand, which is how the last few sessions'
    # own throughput bugs actually got caught).
    max_workers = os.cpu_count() or 8
    if "--max-workers" in sys.argv:
        max_workers = int(sys.argv[sys.argv.index("--max-workers") + 1])

    render_dir.mkdir(parents=True, exist_ok=True)
    out_csv = render_dir.parent / (render_dir.name + "_results.csv")
    live_log = render_dir.parent / (render_dir.name + "_live.log")
    live_log.write_text("")

    paths = [l.strip() for l in file_list.read_text().splitlines() if l.strip()]
    # Starting at 4 and growing by the default max_growth_step=4 per tick
    # took several minutes (multiple 30s ticks) to reach a real operating
    # point -- real, observed as "still climbing to 50% CPU a while in."
    # Both raised for this pipeline specifically: a higher starting window
    # (a real subprocess pipeline, not the plain in-process work
    # AdaptiveConcurrency's own defaults were tuned against, so the cost of
    # guessing a bit high and backing off one tick later is small) and a
    # bigger per-tick growth step, so the climb to the real ceiling takes a
    # small handful of ticks, not a dozen.
    controller = AdaptiveConcurrency(initial_window=min(12, max_workers), max_window=max_workers,
                                      max_growth_step=8)
    print(f"{len(paths)} files to render, up to {max_workers} workers (adaptive, "
          f"starting at {controller.window}), live log: {live_log}")

    rows: list[dict] = []
    # A first attempt at 8s/4 samples-per-tick was too tight for this
    # pipeline's own real per-file spread (confirmed against the live log:
    # 2-4s for a plain creature, 15-20s for a heavier one, in the same
    # short stretch) -- one slow file landing in a small-sample tick badly
    # skews that tick's measured throughput, reads as a false "thrashing"
    # signal, and triggers a spurious backoff before the window ever gets
    # the chance to grow. corpus_scan_framework.py's own AdaptiveConcurrency
    # docstring already names this exact failure mode and defaults to 15
    # samples/tick for its own (lighter-weight) per-file cost -- this
    # pipeline's cost is heavier still, so both the sample floor and the
    # tick duration are raised well past that.
    tick_seconds = 30.0
    min_samples_per_tick = 15
    stats_path = render_dir.parent / (render_dir.name + "_stats.json")
    stats_write_interval = 0.5  # separate from tick_seconds -- this is UI refresh cadence, not controller tuning
    with ProcessPoolExecutor(max_workers=max_workers) as pool:
        pending = iter(paths)
        in_flight: dict[Future, str] = {}

        def top_up() -> None:
            while len(in_flight) < controller.window:
                p = next(pending, None)
                if p is None:
                    return
                in_flight[pool.submit(process_one, p, str(render_dir), str(live_log))] = p

        top_up()
        start = time.monotonic()
        tick_start = start
        tick_completed = 0
        done = 0
        last_stats_write = 0.0
        overall_rate = 0.0
        _write_stats(stats_path, total=len(paths), done=0, render_ok=0, max_workers=max_workers,
                     window=controller.window, backoff_count=0, window_trace=controller.trace_str(),
                     recent_rate=0.0, overall_rate=0.0, elapsed_s=0.0)
        while in_flight:
            finished, _ = wait(in_flight, timeout=0.5, return_when=FIRST_COMPLETED)
            for fut in finished:
                in_flight.pop(fut)
                rows.append(fut.result())
                done += 1
                tick_completed += 1
                if done % 20 == 0:
                    ok = sum(1 for r in rows if r["render_ok"])
                    print(f"  {done}/{len(paths)} done, {ok} rendered OK so far, "
                          f"window={controller.window} (backoffs={controller.backoff_count})")

            now = time.monotonic()
            if now - tick_start >= tick_seconds and tick_completed >= min_samples_per_tick:
                rate = tick_completed / (now - tick_start)
                controller.record(now - start, rate)
                tick_start = now
                tick_completed = 0
            if now - last_stats_write >= stats_write_interval:
                overall_rate = done / (now - start) if now > start else 0.0
                ok = sum(1 for r in rows if r["render_ok"])
                _write_stats(stats_path, total=len(paths), done=done, render_ok=ok, max_workers=max_workers,
                             window=controller.window, backoff_count=controller.backoff_count,
                             window_trace=controller.trace_str(), in_flight=len(in_flight),
                             overall_rate=overall_rate, elapsed_s=now - start)
                last_stats_write = now
            top_up()

    print(f"  concurrency window trace: {controller.trace_str()} "
          f"({controller.backoff_count} backoff(s), converged at {controller.window})")
    _write_stats(stats_path, total=len(paths), done=done, render_ok=sum(1 for r in rows if r["render_ok"]),
                 max_workers=max_workers, window=controller.window, backoff_count=controller.backoff_count,
                 window_trace=controller.trace_str(), in_flight=0, overall_rate=overall_rate,
                 elapsed_s=time.monotonic() - start, finished=True)

    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["path", "export_ok", "render_ok", "skipped_no_geometry", "detail"])
        w.writeheader()
        w.writerows(rows)

    ok = sum(1 for r in rows if r["render_ok"])
    print(f"\n{ok}/{len(rows)} rendered successfully ({ok/len(rows):.1%})")
    print(f"results: {out_csv}")
    print(f"live log: {live_log}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
