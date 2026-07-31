# M2_UNKNOWNS_EXPLORATION — investigation brief for the last genuinely undocumented M2 pieces

**Read this whole file before doing anything.** It's written to be
self-contained for a fresh session with no memory of how it was produced —
assume you're picking this up cold.

## What this is

`husk` (this repo) is a WoW M2 model → glTF 2.0 converter. It has already
independently reverse-engineered several M2-family pieces wowdev.wiki
either didn't document at all or documented wrong — see `WIKI_FINDINGS.md`
for the full record (in particular §2 `.anim`'s `AFSB` shape, §4 `.bone`'s
whole file format, §8 `WFV3`'s undocumented short variant, §9 `.phys`'s
full struct set). That's the track record this investigation should try to
extend, using the same methodology, for the handful of pieces that are
still genuinely open. This isn't a from-scratch reverse-engineering
exercise — it's "apply the same technique one more time," and the
technique is well-established in this repo by now.

**What "genuinely open" means here, precisely**: these are not gaps where
husk hasn't gotten around to implementing something documented (that's
`M2_GAPS_TODO.md`, a different file, don't confuse the two). These are
gaps where **wowdev.wiki itself has no field-level struct**, or gives one
that's internally inconsistent, or names a mechanism without giving enough
to actually resolve it. Six targets, described below.

## Environment

- Repo root: this directory. `CLAUDE.md` (this project's own, plus
  `~/.claude/CLAUDE.md` if present in your environment) has the full
  operational ground rules — read it if you haven't. Key ones for this
  task: **no bare `python`/`python3`** — this sandbox has no system
  Python; use `direnv exec . uv run --no-project python3 <script>` (ad
  hoc `-c` one-liners are fine for quick checks; write anything more than
  a few lines to a real file, don't paste growing `-c` scripts turn by
  turn — costs a confirmation prompt every time otherwise). One
  command per shell block. Never run anything against `/` or unbounded
  `find`/scans outside named paths.
- Real corpus: `/media/luna/data/wow_export` — Luna's own WoW extraction,
  ~130,575 real `.m2` files (plus `.skin`/`.skel`/`.bone`/`.phys`
  sidecars). Read-only. This is real, personally-owned data, same
  convention as everything else this project reads from there.
- Reference spec mirror: `documentation/wowdev-wiki/md/M2.md` (and
  `PHYS.md`/`BONE.md` for the sidecar formats) — a local snapshot,
  `wiki_revision: 36976`, `mirrored_at: 2026-07-25`. Treat it as a
  starting point, not ground truth — this project's whole track record is
  finding places where it's wrong or incomplete.
- Existing standalone exploration tools to use as templates, **not** to
  import from — this project's convention is small, self-contained,
  one-off scripts, deliberately not sharing code with husk's own C++
  parser (an independent decoder is the whole point: if your script and
  husk's C++ agree, that's a real cross-check; if your script imports
  husk's logic, it isn't one):
  - `tools/find_multiroot_skeletons.py` — cleanest template for "walk the
    corpus, extract one specific fact per file, write matching paths to a
    `*_for_exploration.txt` file at repo root." Read it before writing
    your own scanner.
  - `tools/corpus_checks.py` — heavier-weight independent M2/`.skin`
    reader, useful if you need more than one field per file.
- **Chunk tag byte order**: M2's own inline Legion+ chunks (the ones this
  investigation is about) are **not** byte-reversed — `M2.md` says this
  explicitly: "Unlike all other chunked formats in WoW, chunk names in M2
  are NOT reversed. Example: AFID == AFID in file." This is the opposite
  convention from `.phys` (WMO/ADT-style reversed tags, see
  `WIKI_FINDINGS.md` §9 and `src/phys.cpp`'s reversed-literal chunk
  constants) — don't carry that reversal habit over by mistake if you've
  read the `.phys` investigation first.
- husk's own current handling of everything below: `src/cmd_dump.cpp`'s
  `kFallback` table (search for `"WFV1"`, `"WFV2"`, `"DPIV"`, `"AFRA"`,
  `"DETL"`) — each has a short note explaining exactly why it's not
  parsed. Read those notes; they're the precise, current state of "what's
  missing," more reliable than anything summarized here if the two ever
  disagree (this file is a snapshot, husk's own source is live).

## General methodology (established by this project, don't reinvent)

1. **Write an independent scanner first**: walk a real sample of the
   corpus (or all of it, if cheap — `find_multiroot_skeletons.py` does
   130k files in ~40s), find every file that actually carries the target
   chunk/field, and get a real count. Don't start hex-dumping before you
   know how much real data exists to check against — several of these
   targets may have zero real instances in this corpus (flag that plainly
   if so, don't force a derivation from nothing).
2. **Cross-check derived structure against many real files, not one.**
   Every successful crack in `WIKI_FINDINGS.md` used a real multi-file
   sample (7–103+ files) and looked for self-consistency: monotonic
   timestamps, finite floats, unit-length quaternions, plausible value
   ranges (colors in `0..1` or `0..255`, alpha as a fraction, etc.), and —
   the single strongest technique used repeatedly — **full byte
   accounting**: if a struct has explicit count fields, compute the
   expected total size field-by-field and assert it equals the chunk's
   real size exactly. This is what caught the `PLYT` header-stride bug
   (§9) and would have caught a wrong `DETL` guess immediately (see
   below).
3. **A struct guess that "mostly" fits the right size is not confirmed.**
   Off-by-a-few-bytes matters — `WFV3`'s real short variant (§8) is
   exactly 16 bytes shorter than the documented struct, discovered
   because someone actually checked the chunk's real size against the
   struct size rather than assuming padding.
4. **Write up whatever you find, cracked or not**, in `WIKI_FINDINGS.md`'s
   established format (see any existing `##` section there): current
   wiki text, proposed correction/addition, evidence. A confirmed "still
   unknown, here's exactly what was tried and ruled out" entry is a
   legitimate, useful outcome — several existing findings entries include
   exactly this shape for sub-questions that stayed open. Don't force an
   answer that isn't real.
5. **If something gets cracked with enough confidence to implement**: add
   it to `M2_GAPS_TODO.md` (or implement it directly if trivial) rather
   than doing so here — this file's job is investigation, not
   implementation, same separation `PHYS_SIDECAR_FINDINGS.md` → `PHYS_TODO.md`
   used.

---

## Target 1 & 2: `WFV1` / `WFV2` — "waterfall" chunks, ≥ 8.2.0.30080

**Current wiki text**: literally `struct WFV1 { // unknown };` and the
same for `WFV2`. Prose context: "Tells that model has PBR-ish stuff and
normal map. It uses separate render path from usual M2." First tested on
a real waterfall, FileDataID 2445860, in 8.2.0. `WFV3` (the *next*
version, ≥ 9.0.1.33978) **is** fully documented and already implemented in
husk (`dumpWfv3`, `WIKI_FINDINGS.md` §8) — meaning you have a real,
confirmed *successor* struct to reason backward from. That's your biggest
asset here: `WFV3`'s fields (`bumpScale`, several `value0_x`/`value1_w`-
style shader-input floats, a `basecolor` `CImVector`, `flags`, more
floats) are very plausibly an extended version of whatever `WFV1`/`WFV2`
already contained — WoW's version-chunk evolution pattern elsewhere in
this file (`BODY`→`BDY2`→`BDY3`→`BDY4`, `SHAP`→`SHP2`) is almost always
"same leading fields, more appended," not "unrelated redesign."

**Concrete steps**:
1. Scanner: find every real file in the corpus carrying a `WFV1` or `WFV2`
   chunk tag (unreversed — see Environment above), record path + chunk
   size for each.
2. If any found: check whether the chunk size is a clean multiple of a
   plausible field-count, and specifically check whether it's a *prefix*
   of `WFV3`'s own 80-byte struct (i.e., does `WFV1`'s/`WFV2`'s size match
   `WFV3`'s size truncated at a sensible field boundary — `bumpScale` +
   the first N `value*` floats?). Decode the leading floats and sanity-
   check them the same way `WFV3` was checked (plausible small
   shader-coefficient magnitudes, not garbage).
3. If zero real files have either chunk: say so plainly in the writeup.
   These may be rare enough (a handful of waterfall-tech models across the
   whole game) that this corpus genuinely doesn't have one. Don't force a
   derivation from nothing — note it as "structurally plausible successor-
   prefix of WFV3, unconfirmed, no real instance found in this corpus" and
   move on.

## Target 3: `DPIV` — unknown, ≥ 11.1.7.60520 (War Within)

**Current wiki text**: "Unknown, seemingly always 32 bytes, mostly empty."
Very little to go on — no field hints at all, not even a guessed purpose.

**Concrete steps**:
1. Scanner: same shape as above, find real `DPIV`-bearing files (War
   Within-era content — check whether the corpus has good coverage of
   that expansion range at all first; if the extraction predates 11.1.7,
   this may simply be unreachable, same as `PCOL` in `M2_GAPS_TODO.md`
   item 4).
2. If found: since the wiki says "mostly empty," look for the *non-zero*
   bytes specifically across several real files — which byte offsets
   actually vary between files? That's a much stronger signal than
   scanning the whole 32 bytes uniformly. Check whether non-zero regions
   correlate with anything else about the model (particle count, light
   count, bone count) the way `TXAC`'s stride correlates with
   `materials.count + particles.count`.
3. Given "mostly empty" and no field hints, this may not be crackable at
   all from real-file analysis alone — a legitimate outcome here is
   "confirmed real files exist, byte offsets X/Y vary, semantics unknown."
   Don't invent field names for bytes that are genuinely opaque.

## Target 4: `AFRA` — "not observed in any files yet," ≥ DF (Dragonflight)

**Current wiki text**: no struct at all, just "Not observed in any files
yet, presumably added in DF."

**Concrete steps**:
1. This one's first question is purely empirical and cheap to answer:
   **does this corpus have any file with an `AFRA` chunk tag at all?** A
   plain scan answers this in the same pass as the `WFV1`/`WFV2`/`DPIV`
   scan above — bundle all four into one scanner run rather than four
   separate ones.
2. If genuinely zero hits: this is a real, confirmed "still doesn't exist
   in this corpus" finding — worth writing up as such (a negative result,
   dated against this corpus's own extraction date, is useful information,
   not a non-finding). Don't spend further time inventing a structure with
   nothing to check it against.
3. If any hits turn up (would itself be new information the wiki doesn't
   have): full derivation from scratch, same discipline as every other
   target — this would be the most "true reverse engineering, no existing
   scaffolding to lean on" target on this list, closer to how `.bone`'s
   investigation started (§4) than `WFV3`'s (which had a known-good sibling
   chunk to compare against).

## Target 5: `DETL` — lighting-related, ≥ 9.0.1.34365

**Current wiki text**: a struct with fields summing to 0x0C bytes
(`uint16_t flags` + `float16 scale` + `float16 diffuseColorMultiplier` +
`uint16_t unk0` + `uint32_t unk1`), but an explicit end-offset comment of
`/*0x0a*/` — a **6-byte discrepancy** (0x0C computed vs 0x0A stated) husk's
own `cmd_dump.cpp` fallback note already calls out as the reason this
isn't parsed. This is the most mechanically tractable target on this
list — it's not "no data at all" like `WFV1`/`WFV2`/`DPIV`, it's "the
wiki's own math doesn't check out, go find out which side is wrong using
real bytes."

**Concrete steps**:
1. Scanner: find real `DETL`-bearing files. Note the array is sized
   `DETL_recs[m2data.header.lights.count]` — i.e. one record per light,
   not a self-describing count of its own. Cross-check: does
   `chunk.size / lights.count` come out to a clean integer? Try both 0x0A
   and 0x0C as the candidate stride and see which one divides evenly (the
   exact technique that resolved `SHOJ`'s stride ambiguity in `.phys`,
   §9, and `PLYT`'s header-stride bug, same section) — if only one of the
   two stride candidates divides evenly across every real file checked,
   that's your answer with real confidence, not a guess.
2. Once the real stride is known, decode fields at that stride and
   sanity-check: `scale`/`diffuseColorMultiplier` as `float16` should
   decode to small, plausible multiplier values (roughly 0–a few, not
   garbage); `flags` as a bitfield should have a small number of distinct
   bit patterns across many real files, not random noise.
3. If the real stride turns out to be genuinely 0x0A: figure out which
   documented field is actually 2 bytes shorter than claimed, or missing
   entirely — `unk1` being `uint16_t` instead of `uint32_t` would exactly
   close a 2-byte gap on its own; check that hypothesis first since it's
   the simplest single-field fix, before assuming a whole field is absent.

## Target 6: `M2Sequence.aliasNext` resolution mechanism

Different shape from the five above — this isn't an unparsed chunk, it's
a **named-but-unresolved field** on an already-fully-understood struct
(see `M2_GAPS_TODO.md` item 1, which will parse `aliasNext` as a raw
`uint16_t` regardless of this investigation's outcome — that part is
mechanical and not blocked on you). What's open is what the value
actually *means*.

**Background, already established** (`TODO_correctness.md` #4 has the
full writeup — read it before starting, don't redo this part): `M2Sequence`
has a real `aliasNext` field (offset `0x22`) and the wiki's own Flags
table names a real mechanism — "the client skips these by following
`aliasNext` until an animation without `0x40` is found" — contradicting an
older, vaguer wiki bullet ("I have no clue") elsewhere on the same page. A
first real check against `bloodelffemale_hd.skel` (7 alias sequences out
of 396) found `aliasNext` values in the 48861–48983 range that:
- don't work as a local array index (too large for a 396-entry array), and
- don't match any other sequence's own `id` field **within that same
  file**.

**What hasn't been tried yet — concrete next steps, in the order
`TODO_correctness.md` #4 itself suggests**:

1. **Check the field name literally.** The wiki's own field comment says
   `aliasNext`: "id in the list of animations." That's "id," not "index" —
   worth deliberately testing the hypothesis that `aliasNext` is meant to
   be matched against `Sequence.id` (the `AnimationData.dbc`-scale id,
   already confirmed to *not* match within the same file) rather than a
   positional index — but check across a **wider id space** than one
   file's own sequences before concluding it's unresolvable: does
   `aliasNext` match `id` on a *sibling* file (another race/gender variant
   sharing the same base skeleton/animation set — e.g.
   `bloodelffemale_hd.skel` vs. `bloodelfmale_hd.skel`, or the base
   `bloodelffemale.m2`'s own inline sequences vs. the `_hd` variant's)?
   Character models share `AnimationData.dbc` ids across all races/genders
   by design (that's the whole point of the shared id space) — a value
   that doesn't resolve locally might resolve globally across the
   model-family's own files, without needing actual DBC access, if the
   id happens to appear as some *other* file's real `Sequence.id`.
2. **Check `variationNext`'s relationship.** `aliasNext` sits right next
   to `variationNext` in the struct (`0x20`/`0x22`) — check whether the
   two are ever both non-`-1`/non-zero on the same record, and whether
   `variationNext`'s own semantics (documented, "-1 or points to an
   Index") give a working pattern to test `aliasNext` against structurally
   (same field width, same struct region, plausible similar mechanism).
3. **Widen the real-file sample.** One model's 7 aliases is a small
   sample. Scan a broader real set (character models are the best target
   — they're the ones most likely to have real alias chains, per the
   existing 7/396 finding) and check: do `aliasNext` values *ever* fall
   in-range for *some* file's own local sequence count, even if not for
   `bloodelffemale_hd` specifically? A single confirmed local-index match
   anywhere would falsify the "definitely global/external" hypothesis
   immediately.
4. **Check prior art.** Community M2 tooling (`wow.export`'s own source,
   `pywowlib`, `WoWDBDefs`, any other open-source M2 reader) may already
   resolve alias sequences, or may skip them the same way husk currently
   does — worth a bounded web search (a couple of queries, per this
   project's own "don't over-spend on this" precedent from the
   multi-root-skeleton investigation) before concluding nobody's solved
   it.
5. **If genuinely unresolvable from file data alone**: that's a legitimate
   conclusion — write it up as "confirmed external, needs
   `AnimationData.dbc`" and fold it into `ENGINE_TODO.md` (the file
   documenting external-data blockers for a downstream engine project,
   not husk itself) rather than leaving it open-ended in
   `TODO_correctness.md` forever.

---

## Deliverables checklist

- [ ] One scanner script covering `WFV1`/`WFV2`/`DPIV`/`AFRA` in a single
      corpus pass (they're cheap to bundle — one chunk-tag walk, four
      tags to check per file).
- [ ] A `DETL`-specific stride-disambiguation check (reuses the same
      corpus-walking approach, different chunk tag, different analysis).
- [ ] A `aliasNext` cross-file id-matching check (different shape
      entirely — this one needs `Sequence.id` extraction across multiple
      related files, not a single-chunk presence scan).
- [ ] `WIKI_FINDINGS.md` updated with one new numbered section per target
      that reached *any* conclusion (including "confirmed still unknown,
      here's what was ruled out") — follow the existing section format
      exactly (current text / proposed addition / evidence).
- [ ] Anything cracked with enough confidence to implement: added to
      `M2_GAPS_TODO.md`, not implemented inline in this investigation.
- [ ] `TODO_correctness.md` #4 updated (not necessarily closed) once
      `aliasNext` has a real disposition, whatever it turns out to be.
- [ ] This file (`M2_UNKNOWNS_EXPLORATION.md`) updated to mark resolved
      targets, or deleted outright once every target has a final
      disposition — same lifecycle every other investigation-then-TODO
      file in this repo has used (`PHYS_SIDECAR_FINDINGS.md` →
      `PHYS_TODO.md` → deleted; `MULTIROOT_SKELETON_TODO.md` → deleted
      once implemented). Don't leave it around half-`[DONE]`.
