const state = { category: 'all', status: 'all', era: 'all', q: '', offset: 0, limit: 80, loading: false, hasMore: true, atTop: true };
const grid = document.getElementById('grid');
const empty = document.getElementById('empty');
const banner = document.getElementById('banner');

function qs(obj) { return Object.entries(obj).filter(([,v]) => v !== '' && v !== undefined)
  .map(([k,v]) => `${k}=${encodeURIComponent(v)}`).join('&'); }

// Real animated previews (render_glb.py's .webm output -- a genuinely-
// animated model, not a static-still thumbnail) play inline, muted/looped,
// instead of requiring a click through to the dedicated 3D viewer to see
// any motion at all -- adopted from Luna's own interactive-viewer work,
// same "husk exports/renders real motion, the browsing UI should actually
// show it" principle. Only plays while the tile is actually on screen
// (videoObserver below) -- with a full corpus page potentially holding 80
// video elements at once, letting every one of them decode/composite
// off-screen would be real, needless GPU/CPU cost, the same "lazy" spirit
// `img.loading = 'lazy'` already has for still images, just implemented by
// hand since <video> has no native lazy-autoplay equivalent.
const videoObserver = new IntersectionObserver(entries => {
  for (const entry of entries) {
    const video = entry.target;
    if (entry.isIntersecting) video.play().catch(() => {});
    else video.pause();
  }
}, { rootMargin: '200px' });

// render_glb.py no longer pads a short clip up to a minimum render length
// (see its own RENDER_MAX_WINDOW_SECONDS doc comment) -- a native clip
// under this threshold (12 frames at render_glb.py's own RENDER_FPS=24,
// i.e. half a second) loops fast enough to read as a flicker rather than
// motion if autoplayed the same way a normal-speed clip is. Rather than
// solving that at render time (padding the file doesn't change how fast
// the *same* short cycle repeats, it just relocates the repetition from
// the player to the encoder, for no visual difference -- see
// render_glb.py's own doc comment for the full reasoning), a clip this
// short starts paused with an explicit play/pause control instead of
// autoplaying -- the viewer opts in to watching a fast loop rather than
// having it forced on them. Real duration is read from the video element
// itself (loadedmetadata) once it's known, not guessed from anything the
// server sent.
const FAST_LOOP_THRESHOLD_SECONDS = 0.5;

function figureFor(item, batchIndex) {
  const fig = document.createElement('figure');
  // Cascading materialize wave, capped so a big batch (e.g. first load)
  // doesn't leave the last row waiting seconds to appear.
  if (batchIndex !== undefined) {
    fig.style.setProperty('--fig-delay', `${Math.min(batchIndex * 22, 480)}ms`);
  }
  if (item.status && item.status.startsWith('FAIL')) fig.classList.add('bad');
  else if (item.status === 'SKIP') fig.classList.add('skip');

  const src = '/img/' + item.rel.split('/').map(encodeURIComponent).join('/');
  const media = item.is_video ? document.createElement('video') : document.createElement('img');
  media.classList.add('glb-trigger');
  media.title = item.detail || item.rel;

  const cap = document.createElement('figcaption');
  cap.innerHTML = `<span class="cat">${item.cat}</span>${item.rel}`;

  let playBtn = null;
  if (item.is_video) {
    media.muted = true;
    media.loop = true;
    media.playsInline = true;
    media.preload = 'metadata';  // don't fetch full video bytes for an off-screen tile
    media.addEventListener('loadeddata', () => media.classList.add('loaded'));

    // Real duration, known only once metadata has actually loaded -- decides
    // autoplay-when-visible (the normal case) vs. paused-until-clicked (a
    // clip under FAST_LOOP_THRESHOLD_SECONDS, see that constant's own doc
    // comment for why padding the file isn't the fix for this).
    playBtn = document.createElement('button');
    playBtn.className = 'fast-loop-btn';
    playBtn.textContent = '▶ play fast loop';
    playBtn.style.display = 'none';
    playBtn.addEventListener('click', (e) => {
      e.stopPropagation();  // don't also trigger the thumbnail's open-3D-viewer click handler
      if (media.paused) { media.play().catch(() => {}); playBtn.textContent = '⏸ pause'; }
      else { media.pause(); playBtn.textContent = '▶ play fast loop'; }
    });
    media.addEventListener('loadedmetadata', () => {
      if (media.duration && media.duration < FAST_LOOP_THRESHOLD_SECONDS) {
        playBtn.style.display = 'block';
      } else {
        videoObserver.observe(media);
      }
    });
    media.src = src;

    const badge = document.createElement('span');
    badge.className = 'video-badge';
    badge.textContent = 'animated';
    fig.appendChild(badge);
  } else {
    media.loading = 'lazy';
    media.decoding = 'async';
    media.onload = () => media.classList.add('loaded');
    media.src = src;
  }

  // Clicking a thumbnail opens the sibling <stem>.glb in a dedicated tab
  // (the full interactive three.js viewer -- real orbit controls, not just
  // the autoplay loop this tile shows inline).
  media.addEventListener('click', () => {
    const viewerUrl = '/viewer?rel=' + encodeURIComponent(item.rel);
    window.open(viewerUrl, '_blank', 'noopener');
  });

  fig.appendChild(media); fig.appendChild(cap);
  if (playBtn) cap.appendChild(playBtn);
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
