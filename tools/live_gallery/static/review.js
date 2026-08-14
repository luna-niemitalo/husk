// Grid index -> tile, laid out to match the physical numpad exactly:
//   7 8 9        (index 0 1 2)
//   4 5 6   -->  (index 3 4 5)
//   1 2 3        (index 6 7 8)
const NUMPAD_DIGIT_TO_INDEX = {7:0, 8:1, 9:2, 4:3, 5:4, 6:5, 1:6, 2:7, 3:8};
const INDEX_TO_LABEL = ['7','8','9','4','5','6','1','2','3'];
const LIMIT = 9;
// How many pages to keep buffered past what's on screen. Each fetch is a
// sub-millisecond localhost round trip, but chaining them (page N+1 needs
// page N's own "next_after" cursor, see server's /api/review/next) still
// costs a network hop each -- buffering several deep is what actually
// removes the "wait after I press Enter" feeling at a fast, steady
// clicking pace.
const PREFETCH_DEPTH = 4;

// Persisted purely for UX convenience (reopen on the same filter you left
// on) -- NOT relied on for review-progress correctness. That used to be a
// client-held "after" cursor and it had a real clobbering bug (switching
// the filter clobbered it for every other filter). Correctness now lives
// entirely server-side, keyed off the JSONL decision log.
const CATEGORY_KEY = 'review_category';

const state = {
  category: localStorage.getItem(CATEGORY_KEY) || 'all',
  pageHistory: [],    // [{items, flags: Set, afterBefore, nextAfter}, ...] -- every page ever shown, never trimmed
  viewIndex: -1,       // index into pageHistory currently displayed
  prefetchQueue: [],   // pages fetched past the live edge, not yet shown -- see fillPrefetchQueue()
  prefetching: false,  // guards against overlapping fillPrefetchQueue() runs
  submitting: false,
  navigating: false,   // guards against overlapping arrow-key navigateRelative() runs
};

function imgUrl(rel) { return '/img/' + rel.split('/').map(encodeURIComponent).join('/'); }

// Fetching a page's *metadata* ahead of time (the item list) doesn't
// actually remove the wait -- the browser only starts pulling bytes once a
// real <img>/<video> lands in the DOM, i.e. exactly when the page becomes
// current. Off-DOM Image() objects trigger the same fetch early. Note this
// server's shared /img/ route sends Cache-Control: no-store (the same
// route is also used by the live-rescanning main gallery, where a file can
// be legitimately rewritten mid-render-job) -- so unlike the original
// standalone tag_review_server.py this preload no longer produces a true
// cache hit, just an earlier-started fetch; still a real (if smaller) win
// at human reaction pace, and harmless. Videos aren't preloaded this way
// at all -- Image() can't fetch a .webm, and a throwaway off-DOM <video>
// wouldn't get any more benefit than the no-store <img> case does -- they
// just start loading once their real tile enters the DOM (see figureFor).
function preloadImages(items) {
  items.forEach(item => { if (!item.is_video) new Image().src = imgUrl(item.rel); });
}

async function fetchPage(after, category) {
  const res = await fetch(`/api/review/next?after=${encodeURIComponent(after)}&category=${encodeURIComponent(category)}&limit=${LIMIT}`);
  const data = await res.json();
  preloadImages(data.items);
  return { items: data.items, flags: new Set(), afterBefore: after, nextAfter: data.next_after };
}

// Builds the <img> or <video> element for one grid tile. Videos autoplay
// muted/looped straight away -- every review page is capped at 9 tiles and
// only the current page is ever in the DOM at once (unlike the main
// gallery's own IntersectionObserver-gated play/pause, built for a much
// larger always-on-screen grid), so there's no off-screen-decode cost to
// guard against here.
function mediaFor(item) {
  const src = imgUrl(item.rel);
  if (item.is_video) {
    const video = document.createElement('video');
    video.src = src;
    video.muted = true;
    video.loop = true;
    video.autoplay = true;
    video.playsInline = true;
    video.preload = 'auto';
    return video;
  }
  const img = document.createElement('img');
  img.src = src;
  img.loading = 'eager';
  img.decoding = 'async';
  return img;
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
    tile.className = 'tile' + (flagged ? ' flagged' : '') + (item.is_video ? ' video' : '');
    tile.appendChild(mediaFor(item));
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

// The JSONL decision log on disk is the single source of truth for what's
// flagged -- not this tab's own in-memory Set, which only ever reflects
// what *this* tab last staged or was told. Every time navigation lands on
// a page (arrow keys, browser back/forward, or Enter stepping onto an
// already-visited history entry), the displayed flags are re-fetched from
// the server rather than trusted from memory -- an edit made on a page but
// never actually submitted (via Enter) must NOT appear flagged next time
// that page is shown, and a page submitted earlier this session must show
// exactly what was saved, not a leftover local mutation. A page that comes
// back with no entry for a given rel simply isn't reviewed yet (server
// omits it, see ReviewLog.flags_for's own doc comment) -- that's the same
// "start unflagged" state a freshly-fetched page already has, so those
// rels are left out of the rebuilt Set.
async function fetchFlags(rels) {
  const qs = rels.map(r => `rel=${encodeURIComponent(r)}`).join('&');
  const res = await fetch(`/api/review/status?${qs}`);
  const data = await res.json();
  return data.flags;
}

async function syncPageFlagsFromServer(page) {
  if (!page || page.items.length === 0) return;
  const flags = await fetchFlags(page.items.map(it => it.rel));
  page.flags = new Set(Object.entries(flags).filter(([, v]) => v).map(([rel]) => rel));
}

// Shared by Enter (after submitting) and the right-arrow/forward case
// (no submit at all) -- moves one page forward, either into already-
// fetched history or by pulling the next one off the prefetch queue (or a
// direct fetch if the queue hasn't caught up), then keeps the queue topped
// back up in the background.
async function advanceToNextPage() {
  const page = currentPage();
  if (!page) return;
  if (state.viewIndex < state.pageHistory.length - 1) {
    state.viewIndex++;
  } else {
    if (state.prefetchQueue.length === 0) await fillPrefetchQueue();
    const next = state.prefetchQueue.shift() || await fetchPage(page.nextAfter, state.category);
    state.pageHistory.push(next);
    state.viewIndex++;
    fillPrefetchQueue();
  }
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
  fetch('/api/review/submit', { method: 'POST', headers: {'Content-Type': 'application/json'},
                                 body: JSON.stringify({ decisions }) });
  bumpStats(decisions);

  await advanceToNextPage();
  await syncPageFlagsFromServer(currentPage());
  render();
  pushHistoryState();
  state.submitting = false;
}

// Arrow keys are pure navigation -- never submit, never persist whatever's
// currently toggled on the page being left. Left revisits an earlier page
// in this tab's own history (same reach as browser Back, just keyboard-
// driven and without needing the mouse anywhere near the browser chrome);
// Right moves forward, pulling a fresh page off the prefetch queue past
// the live edge exactly like Enter would, just without a submit attached.
// Either way the landing page's flags are re-synced from the server, not
// whatever this tab happened to leave toggled last time it was shown.
async function navigateRelative(delta) {
  if (state.submitting || state.navigating) return;
  if (delta < 0) {
    if (state.viewIndex <= 0) return;
    state.navigating = true;
    state.viewIndex--;
  } else {
    if (!currentPage()) return;
    state.navigating = true;
    await advanceToNextPage();
  }
  await syncPageFlagsFromServer(currentPage());
  render();
  pushHistoryState();
  state.navigating = false;
}

window.addEventListener('popstate', async (e) => {
  if (!e.state || typeof e.state.viewIndex !== 'number') return;
  state.viewIndex = Math.max(0, Math.min(e.state.viewIndex, state.pageHistory.length - 1));
  await syncPageFlagsFromServer(currentPage());
  render();
});

document.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); submitAndAdvance(); return; }
  if (e.key === 'ArrowLeft') { e.preventDefault(); navigateRelative(-1); return; }
  if (e.key === 'ArrowRight') { e.preventDefault(); navigateRelative(1); return; }
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
  const res = await fetch('/api/review/meta');
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
