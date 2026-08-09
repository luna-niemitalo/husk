#!/usr/bin/env python3
"""Live, disk-scanning image gallery server -- the dynamic counterpart to
a one-shot Artifact gallery. An Artifact bakes a fixed file list in at
publish time; this walks a directory on disk on a timer instead, so it
keeps discovering new images (e.g. a corpus render job filling in PNGs
over hours) without ever being republished.

Same filtering shape as the corpus_reports/gallery_src Artifact this
mirrors (category chips, free-text path search) plus one this project's
render_sample_driver.py enables that a static gallery couldn't: a status
filter (OK/FAIL/SKIP), joined in from the driver's own live log by path.

Stdlib only, no flake/dependency changes needed. Read-only: never writes
anything outside its own in-memory index.

Usage:
    tools/venv/bin/python tools/live_gallery_server.py \\
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

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".gif"}

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
                if ext not in IMAGE_EXTS:
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
                items.append({"rel": rel, "cat": cat, "era": era, "mtime": mtime})

        # Status is keyed by the *source* path the driver logged (an .m2
        # under CORPUS_ROOT), not the rendered image's own relative path --
        # join by basename, stripping only the render's own extension
        # (render_sample_driver.py names outputs "<source-basename>.webp" or,
        # for images rendered before that switch, "<source-basename>.png" --
        # matching on the stem keeps both eras joinable through one path).
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
    """render_sample_driver.py logs the *source* .m2 path; the image it
    wrote lives at "<same path relative to CORPUS_ROOT>.<ext>" under the
    render root, `ext` being "webp" (current) or "png" (renders from before
    that switch) -- both stripped to the same source basename by the
    caller's own os.path.splitext, so this only needs to key by that
    basename, not guess an extension. We don't know CORPUS_ROOT here (this
    tool is deliberately generic, not husk-specific) -- so match by
    basename instead of full path: a log entry for ".../foo/bar.m2" is
    joined to any indexed image whose own stem is "bar.m2". Ambiguous only
    if two different source trees share an identical basename, which
    doesn't happen in this corpus's layout.
    """
    by_basename: dict[str, tuple[str, str]] = {}
    for path, (status, detail) in log_status.items():
        by_basename[os.path.basename(path)] = (status, detail)
    return by_basename


PAGE_HTML = """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Live Gallery</title>
<style>
  :root {
    --bg: #0f1115; --panel: #161922; --border: #262b38; --text: #e6e8ef;
    --muted: #8a90a3; --accent: #5da8ff; --good: #3ecf8e; --bad: #ff6b6b; --warn: #f2b84b;
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--text); font: 14px/1.5 -apple-system, "Segoe UI", sans-serif; }
  header { position: sticky; top: 0; z-index: 5; background: rgba(15,17,21,0.92); backdrop-filter: blur(6px);
           border-bottom: 1px solid var(--border); padding: 12px 20px; }
  h1 { font-size: 15px; margin: 0 0 8px; color: var(--muted); font-weight: 600; letter-spacing: 0.02em; }
  h1 b { color: var(--text); }
  .row { display: flex; gap: 8px; flex-wrap: wrap; align-items: center; }
  input[type=text] { background: var(--panel); border: 1px solid var(--border); color: var(--text);
                      border-radius: 8px; padding: 7px 10px; font-size: 13px; min-width: 220px; }
  .chip { background: var(--panel); border: 1px solid var(--border); color: var(--muted); border-radius: 20px;
          padding: 5px 12px; font-size: 12.5px; cursor: pointer; user-select: none; white-space: nowrap; }
  .chip:hover { border-color: var(--accent); color: var(--text); }
  .chip.active { background: var(--accent); border-color: var(--accent); color: #05121f; font-weight: 600; }
  .chip .n { opacity: 0.7; margin-left: 5px; }
  #banner { display: none; margin-left: auto; background: var(--good); color: #05221a; font-weight: 600;
            border-radius: 8px; padding: 6px 12px; cursor: pointer; font-size: 12.5px; }
  #banner.show { display: inline-block; }
  #stats-bar { display: flex; gap: 18px; flex-wrap: wrap; align-items: center; margin-top: 10px;
               padding-top: 10px; border-top: 1px solid var(--border); font-size: 12px; color: var(--muted); }
  #stats-bar .stat { display: flex; align-items: baseline; gap: 5px; white-space: nowrap; }
  #stats-bar .stat b { color: var(--text); font-size: 13px; font-variant-numeric: tabular-nums; }
  #stats-bar .progress-track { width: 140px; height: 6px; border-radius: 3px; background: var(--panel);
                                border: 1px solid var(--border); overflow: hidden; }
  #stats-bar .progress-fill { height: 100%; background: var(--accent); border-radius: 3px 0 0 3px;
                               transition: width 0.6s ease-out; }
  #stats-bar .gpu-dot { display: inline-block; width: 7px; height: 7px; border-radius: 50%; margin-right: 3px; }
  #stats-bar.stale { opacity: 0.45; }
  .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 12px; padding: 16px 20px; }
  figure { margin: 0; background: var(--panel); border: 1px solid var(--border); border-radius: 10px; overflow: hidden;
           animation: fig-in 0.5s ease-out both; animation-delay: var(--fig-delay, 0s); }
  /* A little Code Lyoko materialization homage: a cyan digital flash that
     resolves into the normal panel border, staggered per-item (--fig-delay,
     set in JS) so a batch fills in a cascading wave instead of popping in
     as one flat block. */
  @keyframes fig-in {
    0%   { opacity: 0; transform: translateY(6px) scale(0.97); border-color: #5ff2ff; box-shadow: 0 0 12px 1px rgba(95,242,255,0.55); }
    60%  { opacity: 1; transform: none; border-color: #5ff2ff; box-shadow: 0 0 6px 0 rgba(95,242,255,0.3); }
    100% { opacity: 1; transform: none; border-color: var(--border); box-shadow: none; }
  }
  figure img { width: 100%; height: 160px; object-fit: contain; background: #05070a; display: block;
               opacity: 0; transition: opacity 0.25s ease-out; }
  figure img.loaded { opacity: 1; }
  figcaption { padding: 7px 9px; font-size: 11px; color: var(--muted); word-break: break-all; }
  figcaption .cat { color: var(--accent); font-weight: 600; margin-right: 5px; }
  figure.bad { border-color: var(--bad); }
  figure.skip { border-color: var(--warn); }
  #sentinel { height: 1px; }
  #empty { padding: 40px; text-align: center; color: var(--muted); display: none; }
  #status-chips .chip[data-status=FAIL-EXPORT], #status-chips .chip[data-status=FAIL-RENDER] { }
</style>
</head>
<body>
<header>
  <h1><b id="grand-total">-</b> images indexed &middot; live-scanning <code id="root-path"></code></h1>
  <div class="row">
    <input type="text" id="search" placeholder="filter by path...">
    <div id="cat-chips" class="row"></div>
    <div id="status-chips" class="row"></div>
    <div id="banner">new files available -- click to refresh</div>
  </div>
  <div class="row" id="era-row" style="display:none;">
    <span style="color:var(--muted);font-size:12px;">world/ era:</span>
    <div id="era-chips" class="row"></div>
  </div>
  <div id="stats-bar" style="display:none;"></div>
</header>
<div class="grid" id="grid"></div>
<div id="empty">no images match this filter (yet)</div>
<div id="sentinel"></div>
<script>
const state = { category: 'all', status: 'all', era: 'all', q: '', offset: 0, limit: 80, loading: false, hasMore: true, atTop: true };
const grid = document.getElementById('grid');
const empty = document.getElementById('empty');
const banner = document.getElementById('banner');

function qs(obj) { return Object.entries(obj).filter(([,v]) => v !== '' && v !== undefined)
  .map(([k,v]) => `${k}=${encodeURIComponent(v)}`).join('&'); }

function figureFor(item, batchIndex) {
  const fig = document.createElement('figure');
  // Cascading materialize wave, capped so a big batch (e.g. first load)
  // doesn't leave the last row waiting seconds to appear.
  if (batchIndex !== undefined) {
    fig.style.setProperty('--fig-delay', `${Math.min(batchIndex * 22, 480)}ms`);
  }
  if (item.status && item.status.startsWith('FAIL')) fig.classList.add('bad');
  else if (item.status === 'SKIP') fig.classList.add('skip');
  const img = document.createElement('img');
  img.loading = 'lazy';
  img.decoding = 'async';
  img.onload = () => img.classList.add('loaded');
  img.src = '/img/' + item.rel.split('/').map(encodeURIComponent).join('/');
  img.title = item.detail || item.rel;
  const cap = document.createElement('figcaption');
  cap.innerHTML = `<span class="cat">${item.cat}</span>${item.rel}`;
  fig.appendChild(img); fig.appendChild(cap);
  return fig;
}

// Tracks which rel paths are currently rendered in the grid, so a live
// update (new files landing mid-render-job) can prepend just the new ones
// instead of wiping and rebuilding the whole grid -- that full-rebuild path
// used to fire on every single SSE message, forcing every visible thumbnail
// to re-fetch/re-decode/repaint at once, which is exactly what made the
// page feel stuttery during an active run.
state.knownRels = new Set();

async function loadPage(reset) {
  if (state.loading || (!state.hasMore && !reset)) return;
  state.loading = true;
  if (reset) { state.offset = 0; grid.innerHTML = ''; state.hasMore = true; state.knownRels.clear(); }
  const res = await fetch('/api/files?' + qs({ category: state.category, status: state.status, era: state.era,
                                                 q: state.q, offset: state.offset, limit: state.limit }));
  const data = await res.json();
  document.getElementById('grand-total').textContent = data.grand_total.toLocaleString();
  renderChips(data.categories);
  renderEraChips(data.eras, data.era_labels);
  const frag = document.createDocumentFragment();
  data.items.forEach((item, i) => { frag.appendChild(figureFor(item, i)); state.knownRels.add(item.rel); });
  grid.appendChild(frag);
  state.offset += data.items.length;
  state.hasMore = data.has_more;
  empty.style.display = (state.offset === 0) ? 'block' : 'none';
  state.loading = false;
}

// Used only for the "new files landed while you're at the top" live-update
// path -- fetches the current first page and prepends whatever's genuinely
// new (items are sorted newest-first, so this stops at the first item
// already on screen), leaving everything already rendered untouched.
async function prependNew() {
  if (state.loading) return;
  const res = await fetch('/api/files?' + qs({ category: state.category, status: state.status, era: state.era,
                                                 q: state.q, offset: 0, limit: state.limit }));
  const data = await res.json();
  document.getElementById('grand-total').textContent = data.grand_total.toLocaleString();
  renderChips(data.categories);
  renderEraChips(data.eras, data.era_labels);
  const frag = document.createDocumentFragment();
  let added = 0;
  for (const item of data.items) {
    if (state.knownRels.has(item.rel)) break;
    frag.appendChild(figureFor(item, added));
    state.knownRels.add(item.rel);
    added++;
  }
  if (added > 0) {
    grid.prepend(frag);
    state.offset += added;
    empty.style.display = 'none';
  }
}

function renderChips(categories) {
  const wrap = document.getElementById('cat-chips');
  if (wrap.dataset.built === JSON.stringify(Object.keys(categories).sort())) return;
  wrap.dataset.built = JSON.stringify(Object.keys(categories).sort());
  wrap.innerHTML = '';
  const mk = (label, key, n) => {
    const c = document.createElement('div');
    c.className = 'chip' + (state.category === key ? ' active' : '');
    c.innerHTML = `${label}${n !== undefined ? `<span class="n">${n.toLocaleString()}</span>` : ''}`;
    c.onclick = () => { state.category = key; [...wrap.children].forEach(x => x.classList.remove('active'));
                         c.classList.add('active'); loadPage(true); };
    return c;
  };
  const total = Object.values(categories).reduce((a,b) => a+b, 0);
  wrap.appendChild(mk('all', 'all', total));
  Object.entries(categories).sort((a,b) => b[1]-a[1]).forEach(([cat,n]) => wrap.appendChild(mk(cat, cat, n)));
}

function renderEraChips(eras, labels) {
  eras = eras || {}; labels = labels || {};
  const row = document.getElementById('era-row');
  const wrap = document.getElementById('era-chips');
  const keys = Object.keys(eras).sort();
  if (keys.length === 0) { row.style.display = 'none'; return; }
  row.style.display = 'flex';
  const built = JSON.stringify(keys);
  if (wrap.dataset.built === built) return;
  wrap.dataset.built = built;
  wrap.innerHTML = '';
  const mk = (label, key, n) => {
    const c = document.createElement('div');
    c.className = 'chip' + (state.era === key ? ' active' : '');
    c.innerHTML = `${label}${n !== undefined ? `<span class="n">${n.toLocaleString()}</span>` : ''}`;
    c.onclick = () => { state.era = key; [...wrap.children].forEach(x => x.classList.remove('active'));
                         c.classList.add('active'); loadPage(true); };
    return c;
  };
  const total = Object.values(eras).reduce((a,b) => a+b, 0);
  wrap.appendChild(mk('all', 'all', total));
  keys.forEach(k => {
    const num = k.replace('expansion', '');
    const label = labels[num] ? `${k} (${labels[num]})` : k;
    wrap.appendChild(mk(label, k, eras[k]));
  });
}

function renderStatusChips() {
  const wrap = document.getElementById('status-chips');
  ['all', 'OK', 'FAIL-EXPORT', 'FAIL-RENDER', 'SKIP'].forEach(s => {
    const c = document.createElement('div');
    c.className = 'chip' + (state.status === s ? ' active' : '');
    c.textContent = s;
    c.dataset.status = s;
    c.onclick = () => { state.status = s; [...wrap.children].forEach(x => x.classList.remove('active'));
                         c.classList.add('active'); loadPage(true); };
    wrap.appendChild(c);
  });
}
renderStatusChips();

let searchTimer;
document.getElementById('search').addEventListener('input', e => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(() => { state.q = e.target.value; loadPage(true); }, 250);
});

new IntersectionObserver(entries => { if (entries[0].isIntersecting) loadPage(false); })
  .observe(document.getElementById('sentinel'));

window.addEventListener('scroll', () => { state.atTop = window.scrollY < 100; });

banner.onclick = () => { banner.classList.remove('show'); loadPage(true); };

function connectStream() {
  const es = new EventSource('/api/stream');
  es.onmessage = (ev) => {
    const data = JSON.parse(ev.data);
    document.getElementById('grand-total').textContent = data.total.toLocaleString();
    renderChips(data.categories);
    renderEraChips(data.eras, data.era_labels);
    // Only the very first load (grid still empty) needs a full loadPage --
    // every later update just prepends what's new, so the render job
    // filling in images doesn't jank-refresh whatever you're looking at.
    if (state.knownRels.size === 0) loadPage(true);
    else if (state.atTop) prependNew();
    else banner.classList.add('show');
  };
  es.onerror = () => { es.close(); setTimeout(connectStream, 2000); };
}
connectStream();
loadPage(true);

const GPU_COLORS = ['#5da8ff', '#3ecf8e', '#f2b84b', '#ff6b6b'];

function fmtRate(r) { return r >= 10 ? r.toFixed(0) : r.toFixed(1); }

async function pollSystemStats() {
  let data;
  try {
    const res = await fetch('/api/system_stats');
    data = await res.json();
  } catch {
    return;  // transient fetch failure -- next poll retries, no need to flash an error state
  }
  const bar = document.getElementById('stats-bar');
  const driver = data.driver;
  const parts = [];

  if (driver) {
    const pct = driver.total ? (driver.done / driver.total * 100) : 0;
    parts.push(`
      <div class="stat">
        <span class="progress-track"><span class="progress-fill" style="width:${pct.toFixed(2)}%"></span></span>
        <b>${driver.done.toLocaleString()} / ${driver.total.toLocaleString()}</b> (${pct.toFixed(1)}%)
      </div>`);
    parts.push(`<div class="stat">window <b>${driver.window}</b> / ${driver.max_workers}</div>`);
    if (driver.in_flight !== undefined) parts.push(`<div class="stat">in-flight <b>${driver.in_flight}</b></div>`);
    parts.push(`<div class="stat">backoffs <b>${driver.backoff_count}</b></div>`);
    parts.push(`<div class="stat">rate <b>${fmtRate(driver.overall_rate)}</b>/s</div>`);
    if (driver.finished) parts.push(`<div class="stat" style="color:var(--good);font-weight:600;">finished</div>`);
  } else {
    parts.push(`<div class="stat">driver stats: <b>not available</b></div>`);
  }

  (data.gpus || []).forEach((pct, i) => {
    const color = GPU_COLORS[i % GPU_COLORS.length];
    parts.push(`<div class="stat"><span class="gpu-dot" style="background:${color}"></span>GPU${i} <b>${pct}%</b></div>`);
  });
  parts.push(`<div class="stat">render processes <b>${data.render_processes ?? 0}</b></div>`);

  bar.innerHTML = parts.join('');
  bar.style.display = 'flex';
  bar.classList.remove('stale');
}

pollSystemStats();
setInterval(pollSystemStats, 500);
</script>
</body>
</html>
"""


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

        def do_GET(self):
            parsed = urllib.parse.urlsplit(self.path)
            path = parsed.path
            params = urllib.parse.parse_qs(parsed.query)

            if path == "/":
                body = PAGE_HTML.replace("<code id=\"root-path\"></code>",
                                          f'<code id="root-path">{root}</code>').encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
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
                data = target.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "no-store")  # files here can be rewritten by the render job mid-run
                self.end_headers()
                self.wfile.write(data)
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
