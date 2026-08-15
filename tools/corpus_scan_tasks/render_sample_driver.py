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
describes) against the real machine's own core count as its ceiling -- no
manual override exists for this, deliberately: a hand-picked worker count
only second-guesses an algorithm built specifically to find the real
operating point on its own, and has already done exactly that once (capped
a run at 16 workers on a 32-thread machine for no real reason). Chosen
after finding this pipeline's own real per-file cost is heavily
heavy-tailed (a plain item renders in ~2s, a complex character in 50-90s),
the same shape that controller was built for.

Usage:
    direnv exec . tools/venv/bin/python tools/corpus_scan_tasks/render_sample_driver.py \\
        corpus_reports/render_sample.txt corpus_reports/renders
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
import hashlib
import urllib.parse
from PIL import Image
from PIL.PngImagePlugin import PngInfo

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from corpus_scan_framework import AdaptiveConcurrency  # noqa: E402 -- see sys.path.insert above

CORPUS_ROOT = Path("/media/luna/data/wow_export")
HUSK_BIN = Path("/home/luna/dev/husk/build/husk")
BLENDER_BIN = "blender"
RENDER_SCRIPT = Path(__file__).resolve().parent / "render_glb.py"
SCRATCH_DIR = Path("/media/luna/work/cache/husk_corpus_scratch/render_glbs")
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

# freedesktop.org thumbnail spec size buckets -- "normal" (128px) is plenty
# for these (per this driver's own comment above) simple flat-shaded
# 640x480 renders; not using "large"/512px since nothing here needs it and
# it'd roughly quadruple the per-file resize+encode cost across 130k files.
THUMBNAIL_SIZE = "normal"
THUMBNAIL_PIXELS = {"normal": 128, "large": 256, "x-large": 512, "xx-large": 1024}
THUMBNAIL_CACHE_DIR = Path.home() / ".cache" / "thumbnails" / THUMBNAIL_SIZE
# Disabled for the full corpus re-render (2026-08-13, prompted directly:
# "yeet the thumbnail cache generation") -- not a permanent removal, the
# freedesktop-thumbnail-cache machinery below is left intact so this can be
# flipped back on later.
INSTALL_THUMBNAILS = False

def _thumbnail_cache_path(m2_path: Path) -> tuple[Path, str, str]:
    """(cache_png_path, uri, mtime_str) for m2_path's cache entry -- shared
    by both the staleness check and the actual write so the URI/digest
    logic can't drift between the two call sites."""
    uri = "file://" + urllib.parse.quote(str(m2_path.resolve()))
    mtime = str(int(m2_path.stat().st_mtime))
    digest = hashlib.md5(uri.encode()).hexdigest()
    return THUMBNAIL_CACHE_DIR / f"{digest}.png", uri, mtime


def _thumbnail_is_stale(m2_path: Path) -> bool:
    """True if there's no cached thumbnail for m2_path yet, or the one
    that's there was made for a different mtime (re-extracted from
    CASC/MPQ at a different time, patch update, etc). The cached PNG's own
    Thumb::MTime tEXt chunk is the source of truth -- no separate sqlite/
    manifest needed to track this across runs."""
    cache_png, _, mtime = _thumbnail_cache_path(m2_path)
    if not cache_png.exists():
        return True
    try:
        with Image.open(cache_png) as img:
            return img.text.get("Thumb::MTime") != mtime
    except Exception:
        # Corrupt/truncated entry (e.g. a prior run killed mid-write) --
        # treat as stale rather than raising; a bad cache entry is worse
        # than a missing one, and this is not worth aborting a render over.
        return True


def install_thumbnail(m2_path: Path, rendered_image_path: Path) -> None:
    """Writes m2_path's freedesktop thumbnail cache entry from an
    already-rendered image -- no separate render pass, PIL reads .webp
    natively so there's no format-conversion step beyond the resize."""
    cache_png, uri, mtime = _thumbnail_cache_path(m2_path)
    cache_png.parent.mkdir(parents=True, exist_ok=True)
    pixels = THUMBNAIL_PIXELS[THUMBNAIL_SIZE]
    with Image.open(rendered_image_path) as img:
        img = img.convert("RGBA")
        img.thumbnail((pixels, pixels))
        meta = PngInfo()
        meta.add_text("Thumb::URI", uri)
        meta.add_text("Thumb::MTime", mtime)
        # Temp file + atomic rename, same reasoning as _write_stats below:
        # many worker processes write into this one shared cache dir
        # concurrently, and a reader (Dolphin) polling it must never see a
        # half-written PNG. pid in the suffix avoids any cross-process
        # temp-file collision (digest itself can't collide since it's
        # per-file, but two processes racing the *same* file on a re-run
        # could still clash without it).
        tmp = cache_png.with_suffix(f".tmp{os.getpid()}")
        img.save(tmp, "PNG", pnginfo=meta)
        tmp.replace(cache_png)

def _last_lines(text: str, n: int = 6) -> str:
    lines = [l for l in text.splitlines() if l.strip()]
    return " | ".join(lines[-n:])


def process_one(m2_path_str: str, render_dir_str: str, live_log_str: str) -> dict:
    m2_path = Path(m2_path_str)
    render_dir = Path(render_dir_str)
    live_log = Path(live_log_str)
    rel = m2_path.resolve().relative_to(CORPUS_ROOT)
    # rel.stem, not rel.name -- rel.name is "foo.m2" (the source file's own
    # real extension still attached), which used to leak straight into the
    # render output's name ("foo.m2.webp") for no reason, a real naming bug
    # caught directly ("that's not how any of that works... basically the
    # same as if I just exported it with husk" -- husk export's own -o
    # naming never does this). rel.stem strips exactly the source's own
    # extension, same as `husk export foo.m2 -o foo.glb` would name it by
    # hand.
    scratch_glb = SCRATCH_DIR / rel.parent / (rel.stem + ".glb")
    # New renders write .webp for a static model (render_glb.py switched
    # from PNG, lighter for the live gallery) or .webm for an animated one
    # (render_glb.py decides which, only after inspecting the model -- see
    # ANIMATED_TEXTURE_EFFECTS_TODO.md's §1); a .png left over from before
    # the PNG->WebP switch, or from before --listfile existed, still counts
    # as "already done" here too -- only files whose output was
    # deliberately deleted (the missing-texture-gap re-render pass) or
    # never rendered at all get redone. tools/live_gallery/server.py's own
    # basename-join logic (_index_log_by_source_basename) must stay in sync
    # with this naming, since it derives the same stem independently from
    # the driver's own log line.
    out_webp = render_dir / rel.parent / (rel.stem + ".webp")
    out_webm = render_dir / rel.parent / (rel.stem + ".webm")
    out_png_legacy = render_dir / rel.parent / (rel.stem + ".png")
    scratch_glb.parent.mkdir(parents=True, exist_ok=True)
    out_webp.parent.mkdir(parents=True, exist_ok=True)


    if out_webp.exists() or out_webm.exists() or out_png_legacy.exists():
		# Resume support for the full 130k-file run: this job is expected to
        # take hours, so a restart (crash, machine reboot) must not re-do
        # already-rendered files. A prior image is treated as done -- this
        # driver never writes a partial/temp file directly at the output
        # path (Blender's own render.render(write_still=True) is atomic-
        # enough for this purpose: it writes the final file directly, but
        # only after a successful render call returns).
        existing_image = out_webp if out_webp.exists() else (out_webm if out_webm.exists() else out_png_legacy)
        thumb_ok = False
        if not INSTALL_THUMBNAILS:
            thumb_ok = True
        elif _thumbnail_is_stale(m2_path):
            if existing_image is out_webm:
                # install_thumbnail reads via PIL, which cannot open a video
                # file at all -- extracting a real freedesktop thumbnail
                # from a .webm's first frame (an ffmpeg subprocess call)
                # is real, separate work, not attempted here. Treated as
                # "nothing to install," not a failure -- the render itself
                # already succeeded, only the desktop-file-manager thumbnail
                # cache entry is missing for this one file.
                thumb_ok = True
            else:
                try:
                    install_thumbnail(m2_path, existing_image)
                    thumb_ok = True
                except Exception as e:
                    _log(live_log, "FAIL-THUMB", m2_path, f"thumbnail install failed on resume: {e}")
        else:
            thumb_ok = True  # already current, nothing to do
        return {"path": str(m2_path), "export_ok": True, "render_ok": True,
                "skipped_no_geometry": False, "thumb_ok": thumb_ok,
                "detail": "resumed: already rendered"}

    row = {"path": str(m2_path), "export_ok": False, "render_ok": False, "skipped_no_geometry": False,
           "thumb_ok": False, "detail": ""}
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
        # render_glb.py is called with the .webp path regardless -- it only
        # decides for itself, after inspecting the model, whether to write
        # there (static case) or to out_webm instead (animated case, see
        # ANIMATED_TEXTURE_EFFECTS_TODO.md's §1) -- so success/output here
        # must check for either, the same way the resume branch above does.
        if p.returncode == 0 and "SKIPPED no mesh objects" in p.stdout:
            row["skipped_no_geometry"] = True
            _log(live_log, "SKIP", m2_path, "0-vertex model (camera/track-only), nothing to render")
        elif p.returncode != 0 or not (out_webp.exists() or out_webm.exists()):
            row["detail"] = f"render failed: {_last_lines(p.stdout + p.stderr)}"
            _log(live_log, "FAIL-RENDER", m2_path, row["detail"])
        else:
            row["render_ok"] = True
            actual_out = out_webp if out_webp.exists() else out_webm
            _log(live_log, "OK", m2_path, f"{time.monotonic()-t0:.1f}s -> {actual_out}")
            if not INSTALL_THUMBNAILS:
                row["thumb_ok"] = True
            elif actual_out is out_webm:
                # See the resume branch's identical comment above -- PIL
                # can't read a video file, real thumbnail extraction is
                # separate, unimplemented work.
                row["thumb_ok"] = True
            elif _thumbnail_is_stale(m2_path):
                try:
                    install_thumbnail(m2_path, out_webp)
                    row["thumb_ok"] = True
                except Exception as e:
                    row["thumb_ok"] = False
                    _log(live_log, "FAIL-THUMB", m2_path, f"thumbnail install failed: {e}")
            else:
                row["thumb_ok"] = True

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
    # half-written file -- tools/live_gallery/server.py polls it directly
    # for the concurrency controller's own internals (window/backoff/rate),
    # which aren't otherwise observable from outside this process.
    tmp = stats_path.with_suffix(".tmp")
    tmp.write_text(json.dumps({"updated": time.time(), **fields}))
    tmp.replace(stats_path)


def run_render_pipeline(paths: list[str], render_dir: Path) -> int:
    """The actual husk export -> Blender render loop, over an already-
    resolved list of .m2 paths. Factored out of main() so a caller that
    builds its own file list a different way (tools/full_render.py's
    .renderignore-filtered full-corpus discovery, instead of this file's
    own CLI's one-path-per-line text file) gets the exact same tested
    pipeline -- concurrency control, resume support, live log, results
    CSV -- not a second copy of it.
    """
    # No manual override for this, deliberately -- the ceiling is always
    # the real machine's own core count. AdaptiveConcurrency (below) is the
    # whole point: it ramps the live window up/down against measured
    # throughput on its own, exactly the shape corpus_scan_framework.py's
    # own docstring describes it as built for. A hand-picked --max-workers
    # value only ever second-guesses that algorithm with a worse, static
    # guess -- caught directly after a run got capped at 16 on a 32-thread
    # machine for no real reason. Don't reintroduce this flag.
    max_workers = os.cpu_count() or 8

    render_dir.mkdir(parents=True, exist_ok=True)
    out_csv = render_dir.parent / (render_dir.name + "_results.csv")
    live_log = render_dir.parent / (render_dir.name + "_live.log")
    live_log.write_text("")

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
        w = csv.DictWriter(f, fieldnames=["path", "export_ok", "render_ok", "skipped_no_geometry", "thumb_ok", "detail"])
        w.writeheader()
        w.writerows(rows)

    ok = sum(1 for r in rows if r["render_ok"])
    print(f"\n{ok}/{len(rows)} rendered successfully ({ok/len(rows):.1%})")
    print(f"results: {out_csv}")
    print(f"live log: {live_log}")
    return 0


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
    paths = [l.strip() for l in file_list.read_text().splitlines() if l.strip()]
    return run_render_pipeline(paths, render_dir)


if __name__ == "__main__":
    sys.exit(main())
