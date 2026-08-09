#!/usr/bin/env python3
"""Fast, keyboard-driven "captcha style" review server for triaging a large
rendered corpus into flagged/OK -- companion to live_gallery_server.py, not
a replacement (that one is for open-ended browsing/filtering; this one is
for blasting through every single item once, as fast as human reaction time
allows, with zero mouse required).

Shows a fixed 3x3 grid of thumbnails mapped one-to-one onto the physical
numpad (top-left tile = Numpad7, ... bottom-right = Numpad3) -- pure
one-handed operation, no mouse pathing, no looking at the keyboard to find
the right key. Tiles default to unflagged ("looks OK"); press a tile's
numpad key (or click it) to mark it flagged (red X) for closer
investigation. Since real corpora are mostly-fine renders with a minority
of real mistakes, and a missed mistake (false negative) is worse than an
over-cautious flag (false positive), the fast path is: glance, and if
nothing looks wrong, hit Enter -- it submits the whole page as-is and
instantly shows the next one (prefetched in the background, so there's no
network wait between pages, only human reaction time). Backspace
bulk-flags every tile on the current page at once, for the page where
several things are visibly wrong and going tile-by-tile isn't worth it.
The browser's own Back/Forward buttons move purely through pages you've
already seen (no re-fetch, no re-review) -- pressing Enter again while
looking at a past page re-submits its (possibly just-edited) decisions and
steps forward, so revisiting history to fix a slip doesn't require any
special "undo" mode.

Decisions are appended to a JSONL log (--out) as {"rel", "flagged", "ts"};
last write per rel wins. Progress is fully resumable across both a server
restart and a fresh browser tab/reload with zero client-held state: every
page load starts scanning from the top of the (optionally category-
filtered) sorted corpus, and /api/next's own already-reviewed skip (backed
by the JSONL log, the single source of truth) fast-forwards past
everything already decided. An earlier version tried to shortcut this with
a client-side localStorage "resume cursor" -- real, demonstrated bug:
switching the category filter clobbered that cursor for every other
filter, and the filter selection itself wasn't persisted either, so a
session that ever touched the dropdown silently "lost" its resume point.
Scanning from the top costs a linear pass over already-reviewed items each
request, fine at human-interaction pace even deep into a 130k-item corpus
(single-digit milliseconds) -- see is_reviewed() below.

Stdlib only, no flake/dependency changes needed. Read-only against the
image corpus itself; the only write this process performs is appending to
--out.

Usage:
    tools/venv/bin/python tools/tag_review_server.py \\
        --root corpus_reports/renders_full \\
        --out corpus_reports/renders_full_review.jsonl \\
        --port 8009
    then open http://127.0.0.1:8009/
"""
from __future__ import annotations

import argparse
import json
import mimetypes
import os
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".gif"}


class Corpus:
    """Background-rescanned, rel-path-sorted snapshot of every image under
    `root`. Sorted by rel path (not mtime) so review order is stable across
    rescans -- a render job filling in new files mid-review must not
    reshuffle items already shown.
    """

    def __init__(self, root: Path, interval: float):
        self.root = root
        self.interval = interval
        self._lock = threading.Lock()
        self._items: list[dict] = []
        self._categories: dict[str, int] = {}
        self._stop = threading.Event()

    def start(self) -> None:
        self._scan()
        t = threading.Thread(target=self._loop, daemon=True)
        t.start()

    def stop(self) -> None:
        self._stop.set()

    def _loop(self) -> None:
        while not self._stop.wait(self.interval):
            self._scan()

    def _scan(self) -> None:
        items = []
        categories: dict[str, int] = {}
        root = self.root
        for dirpath, _dirnames, filenames in os.walk(root):
            for name in filenames:
                if os.path.splitext(name)[1].lower() not in IMAGE_EXTS:
                    continue
                rel = os.path.relpath(os.path.join(dirpath, name), root)
                cat = rel.split(os.sep, 1)[0] if os.sep in rel else "(root)"
                categories[cat] = categories.get(cat, 0) + 1
                items.append({"rel": rel.replace(os.sep, "/"), "cat": cat})
        items.sort(key=lambda i: i["rel"])
        with self._lock:
            self._items = items
            self._categories = categories

    def snapshot(self) -> tuple[list[dict], dict[str, int]]:
        with self._lock:
            return self._items, dict(self._categories)


class ReviewLog:
    """Append-only JSONL decision log. In-memory state is rebuilt from the
    file on startup (last write per rel wins) so the file itself is the
    single source of truth -- restarting this process never loses or
    duplicates a decision.
    """

    def __init__(self, path: Path):
        self.path = path
        self._lock = threading.Lock()
        self._decisions: dict[str, bool] = {}
        self._load()

    def _load(self) -> None:
        if not self.path.exists():
            return
        with self.path.open("r", errors="replace") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                    self._decisions[rec["rel"]] = bool(rec["flagged"])
                except (ValueError, KeyError):
                    continue

    def submit(self, decisions: list[tuple[str, bool]]) -> None:
        now = time.time()
        with self._lock:
            with self.path.open("a") as f:
                for rel, flagged in decisions:
                    f.write(json.dumps({"rel": rel, "flagged": flagged, "ts": now}) + "\n")
                    self._decisions[rel] = flagged

    def stats(self) -> dict:
        with self._lock:
            total = len(self._decisions)
            flagged = sum(1 for v in self._decisions.values() if v)
        return {"reviewed": total, "flagged": flagged}

    def is_reviewed(self, rel: str) -> bool:
        with self._lock:
            return rel in self._decisions


PAGE_HTML = """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Corpus Review</title>
<style>
  :root {
    --bg: #0b0c10; --panel: #15171d; --border: #262b38; --text: #e6e8ef;
    --muted: #8a90a3; --accent: #5da8ff; --bad: #ff4d4d; --good: #3ecf8e;
  }
  * { box-sizing: border-box; }
  html, body { height: 100%; }
  body { margin: 0; background: var(--bg); color: var(--text); font: 14px/1.4 -apple-system, "Segoe UI", sans-serif;
         display: flex; flex-direction: column; overflow: hidden; }
  header { flex: 0 0 auto; background: var(--panel); border-bottom: 1px solid var(--border);
           padding: 8px 16px; display: flex; align-items: center; gap: 16px; flex-wrap: wrap; }
  header b { color: var(--text); font-variant-numeric: tabular-nums; }
  .muted { color: var(--muted); }
  select { background: var(--panel); border: 1px solid var(--border); color: var(--text);
           border-radius: 6px; padding: 4px 6px; }
  #bar-fill { height: 5px; background: var(--accent); border-radius: 3px; transition: width 0.2s; }
  #bar-track { flex: 1 1 140px; max-width: 240px; height: 5px; background: #05070a; border-radius: 3px; overflow: hidden; }
  main { flex: 1 1 auto; display: flex; overflow: hidden; padding: 10px; gap: 10px; }
  #grid { display: grid; grid-template-columns: repeat(3, 1fr); grid-template-rows: repeat(3, 1fr);
          gap: 8px; flex: 1 1 auto; min-width: 0; height: 100%; }
  .tile { position: relative; border: 3px solid var(--border); border-radius: 8px; overflow: hidden;
          background: #05070a; cursor: pointer; }
  .tile img { width: 100%; height: 100%; object-fit: contain; display: block; }
  .tile.flagged { border-color: var(--bad); }
  .tile.flagged::after { content: "\\2716"; position: absolute; top: 50%; left: 50%; transform: translate(-50%,-50%);
                          font-size: 40px; color: var(--bad); text-shadow: 0 0 8px #000; pointer-events: none; }
  #sidebar { flex: 0 0 300px; display: flex; flex-direction: column; gap: 6px; overflow-y: auto; }
  .info-row { display: flex; align-items: center; gap: 8px; background: var(--panel); border: 2px solid var(--border);
              border-radius: 8px; padding: 6px 8px; cursor: pointer; flex: 1 1 0; min-height: 0; }
  .info-row.flagged { border-color: var(--bad); background: #1f1216; }
  .info-row .key { flex: 0 0 auto; background: #05070a; border: 1px solid var(--border); border-radius: 5px;
                    padding: 2px 8px; font-weight: 700; font-size: 13px; color: var(--text); }
  .info-row .text { min-width: 0; overflow: hidden; }
  .info-row .cat { color: var(--accent); font-size: 11px; font-weight: 600; display: block; }
  .info-row .rel { color: var(--muted); font-size: 11px; word-break: break-all; display: block; }
  .info-row.flagged .rel { color: #ffb3b3; }
  #empty { flex: 1; display: flex; align-items: center; justify-content: center; color: var(--muted); font-size: 18px; }
  footer { flex: 0 0 auto; padding: 5px 16px; background: var(--panel); border-top: 1px solid var(--border);
           color: var(--muted); font-size: 11.5px; display: flex; gap: 16px; flex-wrap: wrap; }
  kbd { background: #05070a; border: 1px solid var(--border); border-radius: 4px; padding: 0 5px; color: var(--text); }
</style>
</head>
<body>
<header>
  <span><b id="reviewed">0</b> / <b id="total">0</b> reviewed</span>
  <div id="bar-track"><div id="bar-fill" style="width:0%"></div></div>
  <span class="muted"><b id="flagged-count">0</b> flagged</span>
  <span class="muted" id="rate">-- / min</span>
  <select id="category"><option value="all">all categories</option></select>
  <span class="muted" id="prefetch-status" style="margin-left:auto;"></span>
</header>
<main>
  <div id="grid"></div>
  <div id="sidebar"></div>
  <div id="empty" style="display:none;">nothing left to review for this filter</div>
</main>
<footer>
  <span><kbd>Numpad 7 8 9 / 4 5 6 / 1 2 3</kbd> toggle flag (matches tile position)</span>
  <span><kbd>Enter</kbd> / <kbd>Space</kbd> submit page &amp; next</span>
  <span><kbd>Backspace</kbd> flag entire page</span>
  <span><kbd>Numpad +</kbd> toggle every tile's flag</span>
  <span>browser <kbd>&larr;</kbd>/<kbd>&rarr;</kbd> revisit past pages</span>
</footer>
<script>
// Grid index -> tile, laid out to match the physical numpad exactly:
//   7 8 9        (index 0 1 2)
//   4 5 6   -->  (index 3 4 5)
//   1 2 3        (index 6 7 8)
const NUMPAD_DIGIT_TO_INDEX = {7:0, 8:1, 9:2, 4:3, 5:4, 6:5, 1:6, 2:7, 3:8};
const INDEX_TO_LABEL = ['7','8','9','4','5','6','1','2','3'];
const LIMIT = 9;
// How many pages to keep buffered past what's on screen. Each fetch is a
// sub-millisecond localhost round trip, but chaining them (page N+1 needs
// page N's own "next_after" cursor, see server's /api/next) still costs a
// network hop each -- buffering several deep is what actually removes the
// "wait after I press Enter" feeling at a fast, steady clicking pace.
const PREFETCH_DEPTH = 4;

// Persisted purely for UX convenience (reopen on the same filter you left
// on) -- NOT relied on for review-progress correctness. See module
// docstring for why: that used to be a client-held "after" cursor and it
// had a real clobbering bug. Correctness now lives entirely server-side.
const CATEGORY_KEY = 'review_category';

const state = {
  category: localStorage.getItem(CATEGORY_KEY) || 'all',
  pageHistory: [],    // [{items, flags: Set, afterBefore, nextAfter}, ...] -- every page ever shown, never trimmed
  viewIndex: -1,       // index into pageHistory currently displayed
  prefetchQueue: [],   // pages fetched past the live edge, not yet shown -- see fillPrefetchQueue()
  prefetching: false,  // guards against overlapping fillPrefetchQueue() runs
  submitting: false,
};

function imgUrl(rel) { return '/img/' + rel.split('/').map(encodeURIComponent).join('/'); }

// Fetching a page's *metadata* ahead of time (the item list) doesn't
// actually remove the wait -- the browser only starts pulling image bytes
// once a real <img> lands in the DOM, i.e. exactly when the page becomes
// current. Off-DOM Image() objects trigger the same fetch early, warming
// the browser's own HTTP cache (server sends Cache-Control: max-age=3600,
// so this is a real cache hit, not a re-request) well before the tiles are
// ever shown.
function preloadImages(items) {
  items.forEach(item => { new Image().src = imgUrl(item.rel); });
}

async function fetchPage(after, category) {
  const res = await fetch(`/api/next?after=${encodeURIComponent(after)}&category=${encodeURIComponent(category)}&limit=${LIMIT}`);
  const data = await res.json();
  preloadImages(data.items);
  return { items: data.items, flags: new Set(), afterBefore: after, nextAfter: data.next_after };
}

function currentPage() { return state.pageHistory[state.viewIndex] || null; }

function render() {
  const grid = document.getElementById('grid');
  const sidebar = document.getElementById('sidebar');
  const empty = document.getElementById('empty');
  const page = currentPage();
  if (!page || page.items.length === 0) {
    grid.style.display = 'none';
    sidebar.style.display = 'none';
    empty.style.display = 'flex';
    return;
  }
  grid.style.display = 'grid';
  sidebar.style.display = 'flex';
  empty.style.display = 'none';
  grid.innerHTML = '';
  sidebar.innerHTML = '';
  for (let i = 0; i < LIMIT; i++) {
    const item = page.items[i];
    const tile = document.createElement('div');
    tile.dataset.idx = i;
    if (!item) { tile.style.visibility = 'hidden'; grid.appendChild(tile); continue; }
    const flagged = page.flags.has(item.rel);
    tile.className = 'tile' + (flagged ? ' flagged' : '');
    const img = document.createElement('img');
    img.src = imgUrl(item.rel);
    img.loading = 'eager';
    img.decoding = 'async';
    tile.appendChild(img);
    tile.onclick = () => toggle(i);
    grid.appendChild(tile);

    // Sidebar rows are in the same 7-8-9/4-5-6/1-2-3 order as the grid
    // tiles they describe -- name/category/key info lives here so the
    // grid itself can stay pure image, maximizing visual fidelity.
    const row = document.createElement('div');
    row.className = 'info-row' + (flagged ? ' flagged' : '');
    row.dataset.idx = i;
    row.innerHTML = `<span class="key">${INDEX_TO_LABEL[i]}</span>
      <span class="text"><span class="cat">${item.cat}</span><span class="rel">${item.rel}</span></span>`;
    row.onclick = () => toggle(i);
    sidebar.appendChild(row);
  }
}

function toggle(i) {
  const page = currentPage();
  if (!page || !page.items[i]) return;
  const rel = page.items[i].rel;
  if (page.flags.has(rel)) page.flags.delete(rel); else page.flags.add(rel);
  document.querySelector(`.tile[data-idx="${i}"]`)?.classList.toggle('flagged');
  document.querySelector(`.info-row[data-idx="${i}"]`)?.classList.toggle('flagged');
}

function flagAll() {
  const page = currentPage();
  if (!page) return;
  page.items.forEach(it => page.flags.add(it.rel));
  document.querySelectorAll('#grid .tile').forEach(t => t.classList.add('flagged'));
  document.querySelectorAll('#sidebar .info-row').forEach(t => t.classList.add('flagged'));
}

// Inverts every tile independently (not a blanket flag-all) -- a second
// press restores the exact prior state, and pressing it once from a
// partially-flagged page reads as "everything except what I already
// marked," a real, distinct case Backspace's flag-all can't express.
function toggleAll() {
  const page = currentPage();
  if (!page) return;
  page.items.forEach(it => {
    if (page.flags.has(it.rel)) page.flags.delete(it.rel); else page.flags.add(it.rel);
  });
  document.querySelectorAll('#grid .tile').forEach(t => t.classList.toggle('flagged'));
  document.querySelectorAll('#sidebar .info-row').forEach(t => t.classList.toggle('flagged'));
}

// Keeps state.prefetchQueue topped up to PREFETCH_DEPTH pages past the
// live edge (the last page in pageHistory). Chained sequentially -- each
// page's own "after" cursor is the previous page's nextAfter -- but that
// chaining happens entirely in the background while the user is looking
// at whatever's currently on screen, so it's invisible latency, not wait.
async function fillPrefetchQueue() {
  if (state.prefetching) return;
  state.prefetching = true;
  try {
    while (state.prefetchQueue.length < PREFETCH_DEPTH) {
      const tail = state.prefetchQueue.length
        ? state.prefetchQueue[state.prefetchQueue.length - 1]
        : state.pageHistory[state.pageHistory.length - 1];
      if (!tail) break;
      document.getElementById('prefetch-status').textContent =
        `buffered ${state.prefetchQueue.length + 1}/${PREFETCH_DEPTH}...`;
      const page = await fetchPage(tail.nextAfter, state.category);
      state.prefetchQueue.push(page);
      if (page.items.length === 0) break;  // caught up to the live corpus edge, nothing more to buffer yet
    }
  } finally {
    document.getElementById('prefetch-status').textContent = `${state.prefetchQueue.length} page(s) buffered`;
    state.prefetching = false;
  }
}

function pushHistoryState() {
  history.pushState({ viewIndex: state.viewIndex }, '', location.pathname);
}

async function loadInitial() {
  const page = await fetchPage('', state.category);
  state.pageHistory = [page];
  state.viewIndex = 0;
  state.prefetchQueue = [];
  render();
  history.replaceState({ viewIndex: 0 }, '', location.pathname);
  fillPrefetchQueue();
}

let reviewedLocal = 0, flaggedLocal = 0;
const startTime = Date.now();
function bumpStats(decisions) {
  reviewedLocal += decisions.length;
  flaggedLocal += decisions.filter(d => d[1]).length;
  document.getElementById('reviewed').textContent = reviewedLocal.toLocaleString();
  document.getElementById('flagged-count').textContent = flaggedLocal.toLocaleString();
  const mins = (Date.now() - startTime) / 60000;
  if (mins > 0.05) document.getElementById('rate').textContent = (reviewedLocal / mins).toFixed(0) + ' / min';
  const total = parseInt(document.getElementById('total').textContent.replace(/,/g, '')) || 0;
  if (total) document.getElementById('bar-fill').style.width = Math.min(100, reviewedLocal / total * 100) + '%';
}

// Enter always (re)submits whatever's on screen, then steps one page
// forward -- either into already-visited history (if the user browsed
// back first) or onto the live, prefetched edge. This keeps a single
// mental model: Enter = "I've looked at this, move on," regardless of
// whether you're seeing it for the first time or fixing an earlier slip.
async function submitAndAdvance() {
  if (state.submitting) return;
  const page = currentPage();
  if (!page) return;
  state.submitting = true;
  const decisions = page.items.map(it => [it.rel, page.flags.has(it.rel)]);
  fetch('/api/submit', { method: 'POST', headers: {'Content-Type': 'application/json'},
                          body: JSON.stringify({ decisions }) });
  bumpStats(decisions);

  if (state.viewIndex < state.pageHistory.length - 1) {
    state.viewIndex++;
  } else {
    if (state.prefetchQueue.length === 0) await fillPrefetchQueue();
    const next = state.prefetchQueue.shift() || await fetchPage(page.nextAfter, state.category);
    state.pageHistory.push(next);
    state.viewIndex++;
    fillPrefetchQueue();
  }
  render();
  pushHistoryState();
  state.submitting = false;
}

window.addEventListener('popstate', (e) => {
  if (!e.state || typeof e.state.viewIndex !== 'number') return;
  state.viewIndex = Math.max(0, Math.min(e.state.viewIndex, state.pageHistory.length - 1));
  render();
});

document.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); submitAndAdvance(); return; }
  if (e.key === 'Backspace') { e.preventDefault(); flagAll(); return; }
  if (e.code === 'NumpadAdd' || e.key === '+') { e.preventDefault(); toggleAll(); return; }
  const isNumpad = e.code && e.code.startsWith('Numpad') && e.code.length === 7;
  const digit = isNumpad ? parseInt(e.code.slice(6)) : (/^[1-9]$/.test(e.key) ? parseInt(e.key) : NaN);
  if (digit in NUMPAD_DIGIT_TO_INDEX) { toggle(NUMPAD_DIGIT_TO_INDEX[digit]); }
});

document.getElementById('category').addEventListener('change', (e) => {
  state.category = e.target.value;
  localStorage.setItem(CATEGORY_KEY, state.category);
  loadInitial();
});

async function loadMeta() {
  const res = await fetch('/api/meta');
  const data = await res.json();
  document.getElementById('total').textContent = data.total.toLocaleString();
  reviewedLocal = data.reviewed;
  flaggedLocal = data.flagged;
  document.getElementById('reviewed').textContent = reviewedLocal.toLocaleString();
  document.getElementById('flagged-count').textContent = flaggedLocal.toLocaleString();
  document.getElementById('bar-fill').style.width = Math.min(100, reviewedLocal / (data.total || 1) * 100) + '%';
  const sel = document.getElementById('category');
  const builtKey = JSON.stringify(Object.keys(data.categories).sort());
  if (sel.dataset.built !== builtKey) {
    sel.dataset.built = builtKey;
    sel.innerHTML = '<option value="all">all categories</option>';
    Object.entries(data.categories).sort((a,b) => b[1]-a[1]).forEach(([cat, n]) => {
      const opt = document.createElement('option');
      opt.value = cat; opt.textContent = `${cat} (${n.toLocaleString()})`;
      sel.appendChild(opt);
    });
    sel.value = state.category;  // restore persisted selection, lost when options were rebuilt
  }
}

loadMeta().then(loadInitial);
setInterval(loadMeta, 30000);
</script>
</body>
</html>
"""


def make_handler(corpus: Corpus, root: Path, log: ReviewLog):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def _json(self, obj, code=200):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _read_json_body(self) -> dict:
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length) if length else b"{}"
            return json.loads(raw)

        def do_GET(self):
            parsed = urllib.parse.urlsplit(self.path)
            path = parsed.path
            params = urllib.parse.parse_qs(parsed.query)

            if path == "/":
                body = PAGE_HTML.encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            if path == "/api/meta":
                items, categories = corpus.snapshot()
                stats = log.stats()
                self._json({"total": len(items), "categories": categories, **stats})
                return

            if path == "/api/next":
                after = (params.get("after") or [""])[0]
                category = (params.get("category") or ["all"])[0]
                limit = min(int((params.get("limit") or ["16"])[0]), 100)
                items, _ = corpus.snapshot()
                if category and category != "all":
                    items = [it for it in items if it["cat"] == category]
                start = 0
                if after:
                    # rel-sorted list -> linear scan is fine at this scale
                    # for an interactive, human-paced tool (see module doc).
                    for i, it in enumerate(items):
                        if it["rel"] == after:
                            start = i + 1
                            break
                    else:
                        start = 0
                out = []
                i = start
                while i < len(items) and len(out) < limit:
                    it = items[i]
                    if not log.is_reviewed(it["rel"]):
                        out.append(it)
                    i += 1
                next_after = out[-1]["rel"] if out else after
                self._json({"items": out, "next_after": next_after, "has_more": i < len(items)})
                return

            if path.startswith("/img/"):
                rel = urllib.parse.unquote(path[len("/img/"):])
                target = (root / rel).resolve()
                try:
                    target.relative_to(root.resolve())
                except ValueError:
                    self.send_error(403, "path escapes corpus root")
                    return
                if not target.is_file():
                    self.send_error(404, "not found")
                    return
                ctype = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
                data = target.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "public, max-age=3600")
                self.end_headers()
                self.wfile.write(data)
                return

            self.send_error(404, "not found")

        def do_POST(self):
            path = self.path
            if path == "/api/submit":
                body = self._read_json_body()
                decisions = [(rel, bool(flagged)) for rel, flagged in body.get("decisions", [])]
                log.submit(decisions)
                self._json({"ok": True})
                return
            self.send_error(404, "not found")

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True, type=Path, help="directory of rendered images to review, recursively")
    ap.add_argument("--out", type=Path, default=None,
                     help="JSONL decision log path (default: <root>_review.jsonl next to --root)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8009)
    ap.add_argument("--interval", type=float, default=15.0, help="filesystem rescan interval in seconds")
    args = ap.parse_args()

    root = args.root.resolve()
    if not root.is_dir():
        print(f"error: --root {root} is not a directory")
        return 1

    out = args.out.resolve() if args.out else root.parent / (root.name + "_review.jsonl")

    corpus = Corpus(root, args.interval)
    corpus.start()
    log = ReviewLog(out)

    server = ThreadingHTTPServer((args.host, args.port), make_handler(corpus, root, log))
    stats = log.stats()
    print(f"serving {root} on http://{args.host}:{args.port}/")
    print(f"decision log: {out} ({stats['reviewed']} reviewed so far, {stats['flagged']} flagged)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        corpus.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
