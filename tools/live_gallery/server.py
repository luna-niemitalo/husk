#!/usr/bin/env python3
"""Live, disk-scanning image/video gallery server -- the dynamic counterpart
to a one-shot Artifact gallery. An Artifact bakes a fixed file list in at
publish time; this walks a directory on disk on a timer instead, so it
keeps discovering new images (e.g. a corpus render job filling in
thumbnails over hours) without ever being republished.

Same filtering shape as the corpus_reports/gallery_src Artifact this
mirrors (category chips, free-text path search) plus one this project's
render_sample_driver.py enables that a static gallery couldn't: a status
filter (OK/FAIL/SKIP), joined in from the driver's own live log by path.

HTML/CSS/JS live under static/ as real, IDE-recognized files (page.*,
viewer.*) -- not embedded Python string literals -- so they get proper
syntax highlighting/formatting in an editor. This module reads them off
disk per request rather than baking them into the process at import time,
matching this tool's own "live, keeps picking up changes" spirit -- edit a
static file and reload the page, no server restart needed. Split out of
the former single-file tools/live_gallery_server.py (2026-08-13), prompted
directly.

Stdlib only, no flake/dependency changes needed. Read-only: never writes
anything outside its own in-memory index.

Usage:
    tools/venv/bin/python tools/live_gallery/server.py \\
        --root corpus_reports/renders_full \\
        --log corpus_reports/renders_full_live.log \\
        --port 8008
    then open http://127.0.0.1:8008/
"""
from __future__ import annotations

import argparse
import json
import mimetypes
import os
import queue
import re
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

STATIC_DIR = Path(__file__).resolve().parent / "static"

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".gif"}
# render_glb.py now writes .webm for a genuinely-animated file (real
# skeletal action or husk's own texture-transform/tint/fade extras --
# ANIMATED_TEXTURE_EFFECTS_TODO.md's §1) instead of a still WebP -- indexed
# and served the same way as every other rendered output, just tagged
# is_video so the grid can play it inline instead of treating it as a
# static thumbnail (see static/page.js's figureFor).
VIDEO_EXTS = {".webm"}

# world/expansionNN/... is a real folder convention in the WoW corpus this
# tool was built against -- confirmed against actual zone/doodad names
# inside each folder (not guessed): expansion01's own subfolders include
# "hellfirepeninsula"/"netherstorm"/"shattrath" (TBC), expansion05 has
# "ironhorde"/"tanaanjungle" (WoD), expansion11 has "playerhousing" (a
# post-The War Within feature, not yet released at time of writing -- least
# certain label here, everything else is a confirmed real zone match). Only
# world/ content is organized this way; creature/character/item/spells have
# no per-file expansion marker at all (a real, separate gap -- would need
# DB2 cross-referencing to close, not attempted here).
WORLD_ERA_RE = re.compile(r"^(expansion(\d\d))(?:[/\\]|$)", re.IGNORECASE)
EXPANSION_LABELS = {
    "01": "The Burning Crusade", "02": "Wrath of the Lich King", "03": "Cataclysm",
    "04": "Mists of Pandaria", "05": "Warlords of Draenor", "06": "Legion",
    "07": "Battle for Azeroth", "08": "Shadowlands", "09": "Dragonflight",
    "10": "The War Within", "11": "post-TWW (unreleased, e.g. player housing)",
}

# One log line looks like "STATUS /abs/path/to/file.m2 :: detail..." --
# see render_sample_driver.py's own _log(). Only the leading token and the
# path are needed here; the detail is surfaced as a tooltip, not parsed further.
LOG_LINE_RE = re.compile(r"^(\S+) (\S+) :: (.*)$")

_DRM_CARD_RE = re.compile(r"^card\d+$")


def _read_gpu_busy_percent() -> list[int]:
    """One entry per real GPU found under /sys/class/drm -- discovered, not
    hardcoded to this machine's own two cards, so this stays useful on any
    box. `card[0-9]*` alone isn't a tight enough glob (it also matches
    connector pseudo-devices like "card0-DP-4"), hence the extra regex
    filter. Best-effort: a GPU whose driver doesn't expose this file (not
    all do) is silently skipped, not an error.
    """
    result = []
    drm = Path("/sys/class/drm")
    if not drm.is_dir():
        return result
    for card_dir in sorted(drm.iterdir()):
        if not _DRM_CARD_RE.match(card_dir.name):
            continue
        try:
            result.append(int((card_dir / "device" / "gpu_busy_percent").read_text().strip()))
        except (OSError, ValueError):
            continue
    return result


def _count_render_processes() -> int:
    """Live count of running render_glb.py Blender subprocesses, read
    straight from /proc -- no psutil dependency, and independent of
    render_sample_driver.py's own stats file (works even against an older
    driver version that doesn't write one).
    """
    count = 0
    proc = Path("/proc")
    if not proc.is_dir():
        return count
    for pid_dir in proc.iterdir():
        if not pid_dir.name.isdigit():
            continue
        try:
            cmdline = (pid_dir / "cmdline").read_bytes()
        except OSError:
            continue
        if b"render_glb.py" in cmdline:
            count += 1
    return count


class SystemStats:
    """Live process/GPU/driver-internals polling, on its own cheap timer --
    independent of Index's own (more expensive, walks the whole image tree)
    rescan loop. Three signals, each degrading gracefully alone:
      - render_sample_driver.py's own "<root>_stats.json" (written next to
        its live log/results CSV, same naming convention) -- the only way
        to see AdaptiveConcurrency's actual internals (window/backoff/rate),
        which aren't observable from outside that process any other way.
        Absent entirely if the driver's running an older version that
        doesn't write one, or hasn't reached its first write yet -- treated
        as "no driver stats available," not an error.
      - GPU busy% per card (real, external, works even if the driver stats
        file is stale or missing).
      - a live render_glb.py process count (ditto).
    """

    def __init__(self, stats_path: Path, interval: float = 0.5):
        self.stats_path = stats_path
        self.interval = interval
        self._lock = threading.Lock()
        self._data: dict = {}
        self._stop = threading.Event()

    def start(self) -> None:
        self._poll()
        t = threading.Thread(target=self._loop, daemon=True)
        t.start()

    def stop(self) -> None:
        self._stop.set()

    def _loop(self) -> None:
        while not self._stop.wait(self.interval):
            self._poll()

    def _poll(self) -> None:
        driver = None
        try:
            driver = json.loads(self.stats_path.read_text())
        except (OSError, ValueError):
            pass
        data = {"driver": driver, "gpus": _read_gpu_busy_percent(), "render_processes": _count_render_processes()}
        with self._lock:
            self._data = data

    def snapshot(self) -> dict:
        with self._lock:
            return dict(self._data)


class Index:
    """Background-refreshed snapshot of every image under `root`, plus an
    optional path->(status, detail) overlay parsed from a driver live log.
    Rebuilt on a timer rather than via filesystem-event watching -- this
    tool has no extra dependencies (no watchdog/inotify library), and a
    plain os.walk poll is cheap enough at this corpus's scale (single-digit
    seconds for ~130k files) to just re-run every few seconds.
    """

    def __init__(self, root: Path, log_paths: list[Path], interval: float):
        self.root = root
        # A crashed-and-resumed driver run truncates its own live log on
        # restart (render_sample_driver.py's live_log.write_text("")) and
        # never re-logs "OK" for files that resume-skip -- so a single
        # current log only covers files touched *since* the last restart.
        # Multiple --log paths (e.g. the live log plus a pre-crash backup)
        # are merged, each independently offset-tracked, so status coverage
        # survives a crash the same way the render output itself does.
        self.log_paths = log_paths
        self.interval = interval
        self._lock = threading.Lock()
        self._items: list[dict] = []
        self._categories: dict[str, int] = {}
        self._eras: dict[str, int] = {}
        self._version = 0
        self._log_offsets: dict[Path, int] = {p: 0 for p in log_paths}
        self._log_status: dict[str, tuple[str, str]] = {}
        self._subscribers: list[queue.Queue] = []
        self._stop = threading.Event()

    def start(self) -> None:
        self._scan_log(full=True)
        self._scan_files()
        t = threading.Thread(target=self._loop, daemon=True)
        t.start()

    def stop(self) -> None:
        self._stop.set()

    def _loop(self) -> None:
        while not self._stop.wait(self.interval):
            self._scan_log(full=False)
            self._scan_files()

    def _scan_log(self, full: bool) -> None:
        for log_path in self.log_paths:
            self._scan_one_log(log_path, full)

    def _scan_one_log(self, log_path: Path, full: bool) -> None:
        if not log_path.exists():
            return
        offset = self._log_offsets[log_path]
        try:
            size = log_path.stat().st_size
            with log_path.open("r", errors="replace") as f:
                if not full and size >= offset:
                    f.seek(offset)
                else:
                    offset = 0  # file truncated/replaced (e.g. driver restart) -- reread from scratch
                new_text = f.read()
                self._log_offsets[log_path] = f.tell()
        except OSError:
            return
        if not new_text:
            return
        with self._lock:
            for line in new_text.splitlines():
                m = LOG_LINE_RE.match(line)
                if m:
                    status, path, detail = m.groups()
                    self._log_status[path] = (status, detail)

    def _scan_files(self) -> None:
        items = []
        categories: dict[str, int] = {}
        eras: dict[str, int] = {}
        root = self.root
        for dirpath, _dirnames, filenames in os.walk(root):
            for name in filenames:
                ext = os.path.splitext(name)[1].lower()
                is_video = ext in VIDEO_EXTS
                if ext not in IMAGE_EXTS and not is_video:
                    continue
                full = os.path.join(dirpath, name)
                try:
                    mtime = os.path.getmtime(full)
                except OSError:
                    continue
                rel = os.path.relpath(full, root)
                cat = rel.split(os.sep, 1)[0] if os.sep in rel else "(root)"
                categories[cat] = categories.get(cat, 0) + 1
                era = ""
                if cat == "world":
                    rest = rel.split(os.sep, 1)[1] if os.sep in rel else ""
                    m = WORLD_ERA_RE.match(rest)
                    if m:
                        era = m.group(1).lower()
                        eras[era] = eras.get(era, 0) + 1
                items.append({"rel": rel, "cat": cat, "era": era, "mtime": mtime, "is_video": is_video})

        # Status is keyed by the *source* path the driver logged (an .m2
        # under CORPUS_ROOT), not the rendered file's own relative path --
        # join by basename, stripping only the render's own extension
        # (render_sample_driver.py names outputs "<source-basename>.webp"
        # for a static model, "<source-basename>.webm" for an animated one,
        # or "<source-basename>.png" for images rendered before the PNG->WebP
        # switch -- matching on the stem keeps all three eras joinable
        # through one path, no extension-specific logic needed here).
        status_by_source_basename: dict[str, tuple[str, str]] = {}
        if self._log_status:
            with self._lock:
                log_status = dict(self._log_status)
            status_by_source_basename = _index_log_by_source_basename(log_status)
        for item in items:
            item_stem = os.path.splitext(os.path.basename(item["rel"]))[0]
            st = status_by_source_basename.get(item_stem)
            item["status"] = st[0] if st else ""
            item["detail"] = st[1] if st else ""

        items.sort(key=lambda i: i["mtime"], reverse=True)

        with self._lock:
            changed = len(items) != len(self._items) or items[:1] != self._items[:1]
            self._items = items
            self._categories = categories
            self._eras = eras
            if changed:
                self._version += 1
                self._notify()

    def _notify(self) -> None:
        payload = {"version": self._version, "total": len(self._items), "categories": self._categories,
                   "eras": self._eras}
        for q in self._subscribers:
            q.put(payload)

    def subscribe(self) -> queue.Queue:
        q: queue.Queue = queue.Queue()
        with self._lock:
            self._subscribers.append(q)
            q.put({"version": self._version, "total": len(self._items), "categories": self._categories,
                   "eras": self._eras})
        return q

    def unsubscribe(self, q: queue.Queue) -> None:
        with self._lock:
            if q in self._subscribers:
                self._subscribers.remove(q)

    def query(self, category: str | None, q: str | None, status: str | None, era: str | None,
              offset: int, limit: int) -> dict:
        with self._lock:
            items = self._items
            version = self._version
            categories = dict(self._categories)
            eras = dict(self._eras)
        needle = q.lower() if q else None
        out = []
        for item in items:
            if category and category != "all" and item["cat"] != category:
                continue
            if status and status != "all" and item["status"] != status:
                continue
            if era and era != "all" and item["era"] != era:
                continue
            if needle and needle not in item["rel"].lower():
                continue
            out.append(item)
        total = len(out)
        page = out[offset:offset + limit]
        return {"version": version, "total": total, "grand_total": len(items),
                "categories": categories, "eras": eras, "era_labels": EXPANSION_LABELS,
                "items": page, "has_more": offset + limit < total}


def _index_log_by_source_basename(log_status: dict[str, tuple[str, str]]) -> dict[str, tuple[str, str]]:
    """render_sample_driver.py logs the *source* .m2 path; the file it
    wrote lives at "<same path relative to CORPUS_ROOT>.<ext>" under the
    render root, `ext` being "webp"/"webm" (current) or "png" (renders from
    before the PNG->WebP switch) -- all stripped to the same source
    basename by the caller's own os.path.splitext, so this only needs to
    key by that basename, not guess an extension. We don't know
    CORPUS_ROOT here (this tool is deliberately generic, not husk-specific)
    -- so match by basename instead of full path: a log entry for
    ".../foo/bar.m2" is joined to any indexed image whose own stem is
    "bar.m2". Ambiguous only if two different source trees share an
    identical basename, which doesn't happen in this corpus's layout.
    """
    by_basename: dict[str, tuple[str, str]] = {}
    for path, (status, detail) in log_status.items():
        by_basename[os.path.basename(path)] = (status, detail)
    return by_basename


def make_handler(index: Index, root: Path, system_stats: SystemStats):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass  # keep terminal quiet; this is a browsing tool, not a diagnostic one

        def _json(self, obj, code=200):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _serve_bytes(self, data: bytes, content_type: str, cache: bool = False) -> None:
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            if not cache:
                self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            parsed = urllib.parse.urlsplit(self.path)
            path = parsed.path
            params = urllib.parse.parse_qs(parsed.query)

            if path == "/":
                body = (STATIC_DIR / "page.html").read_text(encoding="utf-8")
                body = body.replace("<code id=\"root-path\"></code>", f'<code id="root-path">{root}</code>')
                self._serve_bytes(body.encode(), "text/html; charset=utf-8")
                return

            if path == "/api/system_stats":
                self._json(system_stats.snapshot())
                return

            if path == "/api/files":
                category = (params.get("category") or ["all"])[0]
                status = (params.get("status") or ["all"])[0]
                era = (params.get("era") or ["all"])[0]
                q = (params.get("q") or [""])[0]
                offset = int((params.get("offset") or ["0"])[0])
                limit = min(int((params.get("limit") or ["80"])[0]), 500)
                self._json(index.query(category, q, status, era, offset, limit))
                return

            if path == "/api/stream":
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "keep-alive")
                self.end_headers()
                sub = index.subscribe()
                try:
                    while True:
                        try:
                            payload = sub.get(timeout=15)
                            self.wfile.write(f"data: {json.dumps(payload)}\n\n".encode())
                        except queue.Empty:
                            self.wfile.write(b": keepalive\n\n")
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    pass
                finally:
                    index.unsubscribe(sub)
                return

            if path == "/viewer":
                rel = (params.get("rel") or [""])[0]
                if not rel:
                    self.send_error(400, "missing rel")
                    return
                image_target = (root / rel).resolve()
                try:
                    image_target.relative_to(root.resolve())
                except ValueError:
                    self.send_error(403, "path escapes gallery root")
                    return
                if not image_target.is_file():
                    self.send_error(404, "thumbnail not found")
                    return
                body = (STATIC_DIR / "viewer.html").read_text(encoding="utf-8")
                body = body.replace("__REL_JSON__", json.dumps(rel))
                self._serve_bytes(body.encode(), "text/html; charset=utf-8")
                return

            if path.startswith("/static/"):
                # page.html/page.js/page.css/viewer.html/viewer.js/viewer.css --
                # real files on disk now (2026-08-13 split, prompted directly),
                # not embedded Python string constants; served straight from
                # STATIC_DIR with the same path-escape guard every other real-
                # file route here already uses. Re-read per request rather than
                # cached in memory -- this is a live/dev tool, editing one of
                # these files and reloading the page should just work, no
                # server restart needed.
                rel = urllib.parse.unquote(path[len("/static/"):])
                target = (STATIC_DIR / rel).resolve()
                try:
                    target.relative_to(STATIC_DIR)
                except ValueError:
                    self.send_error(403, "path escapes static dir")
                    return
                if not target.is_file():
                    self.send_error(404, "not found")
                    return
                ctype = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
                self._serve_bytes(target.read_bytes(), ctype)
                return

            if path.startswith("/model/"):
                rel = urllib.parse.unquote(path[len("/model/"):])
                target = (root / rel).resolve()
                try:
                    target.relative_to(root.resolve())
                except ValueError:
                    self.send_error(403, "path escapes gallery root")
                    return
                if not target.is_file() or target.suffix.lower() != ".glb":
                    self.send_error(404, "GLB not found")
                    return
                self._serve_bytes(target.read_bytes(), "model/gltf-binary")
                return

            if path.startswith("/img/"):
                rel = urllib.parse.unquote(path[len("/img/"):])
                target = (root / rel).resolve()
                try:
                    target.relative_to(root.resolve())
                except ValueError:
                    self.send_error(403, "path escapes gallery root")
                    return
                if not target.is_file():
                    self.send_error(404, "not found")
                    return
                ctype = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
                # files here can be rewritten by the render job mid-run -- Cache-Control: no-store
                self._serve_bytes(target.read_bytes(), ctype)
                return

            self.send_error(404, "not found")

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True, type=Path, help="directory to scan for images, recursively")
    ap.add_argument("--log", type=Path, default=[], action="append",
                     help="render_sample_driver.py-style live log for OK/FAIL/SKIP status overlay "
                          "(repeatable -- pass both a pre-crash backup and the current live log to "
                          "get full status coverage across a resumed run)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8008)
    ap.add_argument("--interval", type=float, default=3.0, help="filesystem rescan interval in seconds")
    args = ap.parse_args()

    root = args.root.resolve()
    if not root.is_dir():
        print(f"error: --root {root} is not a directory")
        return 1

    log_paths = [p.resolve() for p in args.log]
    index = Index(root, log_paths, args.interval)
    index.start()

    # render_sample_driver.py writes "<render-dir-name>_stats.json" right
    # next to the render dir itself (same naming convention as its own
    # "_live.log"/"_results.csv") -- derived here, not a separate flag, so
    # pointing --root at a driver's own output directory picks this up for
    # free. Harmless if no such file exists (a driver run that predates
    # this, or none running at all): SystemStats.snapshot() just reports
    # driver=None and the page shows only the GPU/process-count signals.
    stats_path = root.parent / (root.name + "_stats.json")
    system_stats = SystemStats(stats_path)
    system_stats.start()

    server = ThreadingHTTPServer((args.host, args.port), make_handler(index, root, system_stats))
    print(f"serving {root} on http://{args.host}:{args.port}/ (rescanning every {args.interval}s)")
    print(f"driver stats file: {stats_path} ({'found' if stats_path.exists() else 'not found yet'})")
    if log_paths:
        print(f"status overlay from {len(log_paths)} log(s): {', '.join(str(p) for p in log_paths)}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        index.stop()
        system_stats.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
