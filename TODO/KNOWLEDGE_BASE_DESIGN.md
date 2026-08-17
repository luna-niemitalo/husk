# DESIGN: a husk-owned DB2/listfile knowledge base

**Status: object-skin resolution FIXED via local fallback, 2026-08-16.**
The DB2 chain (`husk db2-build`/`husk export --knowledge-db`) is real,
mechanically works, and even with the `Item.InventoryType` slot filter
("Known-wrong, not just unverified" below) still produces wrong same-slot
answers often enough not to trust as primary — it stays **disabled**
(`render_sample_driver.py` doesn't pass `--knowledge-db`), kept as
diagnostic/future-work infrastructure, not load-bearing. The actual fix
that unblocked rendering is the "Local fallback" section below: a real,
corpus-verified race/gender-suffix-stripping tier in husk's own existing
fuzzy-basename matcher, no DB2 involved at all. Verified against both
real cases that exposed the DB2 bug — see that section. Written in
response to the object-skin-texture resolution work sprawling across
three independent, direction-specific pieces (a C++ DB2 reader for one
feature, a Python DB2-join script for another, a gatekeeping scan that
silently drops valid candidates) — this file exists to stop and design
instead of adding a fourth piece. Fold the relevant parts back into
`DESIGN.md` and delete this file once the DB2 side is either fixed for
real or deliberately dropped, per this project's established scratch-doc
lifecycle (`DESIGN_CHANGES.md`, `TRANSFORM_TRIAGE.md`,
`VERIFICATION_IDEAS.md` all did this).

## Local fallback (the actual fix, 2026-08-16)

`src/export_texture_resolution.cpp`'s `scanFuzzyTexturePool` already
tried the model's own full basename, then (real, pre-existing) a
`"_sdr"`-stripped basename when that came up empty. Added one more
fallback tier, same shape: `stripRaceGenderSuffix` strips a real,
corpus-verified race/gender suffix (`kRaceCodes` — 20 codes, both
compact `"_bem"` and underscore `"_be_m"` forms, kept only where a real
frequency count across `item/objectcomponents/` was in the hundreds+,
not guessed — see the function's own doc comment for the derivation) and
retries the same directory scan against the stripped family basename.
Zero DB2 involvement, zero new flags — always on, exactly where the
existing `_sdr` tier already lived.

Verified against both real cases that exposed the DB2 chain's
correctness bug:
- `helm_leather_pvpdruid_b_02_scm.m2` → strips to
  `helm_leather_pvpdruid_b_02`, finds the sole real match
  (`helm_leather_pvpdruid_b_02.blp`, the exact file confirmed correct
  earlier), embeds it directly with husk's real, honest
  "non-deterministic basename matching, please confirm" warning — not a
  silent claim of certainty.
- `chest_mail_chainmailset_b_01_go_f.m2` → strips to
  `chest_mail_chainmailset_b_01` (`"go"` is a real race code, Goblin),
  finds **5 real recolor variants**
  (`chest_mail_chainmailset_b_01_be_m_4419259/4264/4265/4266/4267.blp`)
  in the same directory, genuinely ambiguous — all 5 embedded as
  `alternate_textures` extras (husk's existing mechanism,
  `gltf::Material::alternateTextureCandidates`), one picked as default,
  clearly labeled. This is the "recolor variants swappable from the
  output" behavior asked for, for free, since it reuses the same
  ambiguous-candidate machinery the fuzzy matcher already had.

New regression test: `tests/test_integration_weapons.cpp`'s
"race/gender-suffixed model" case (synthetic fixture: a real weapon
model copied under a race-suffixed name, a family-basename PNG placed
alongside it, confirms the fallback actually embeds it).

**Remaining step** (formerly its own `EXPLORATION_TODO.md`, folded in here
since it's this fix's own direct follow-up, not a separate investigation):
`render_sample_driver.py`/`tools/full_render.py` need a real run to
completion — `direnv exec . tools/venv/bin/python tools/full_render.py` —
then a real visual check of the output. Old renders already cleared to
`trash/` for a clean run.

## Known-wrong, not just unverified (2026-08-16)

The original `model_object_skin_texture` join used
`ItemDisplayInfo.ModelMaterialResourcesID_0/1` directly as the texture
source. **This is confirmed wrong** — cross-checked against
`reference/wow.export`'s own real, working implementation
(`DBItemDisplays.js`/`DBItemDisplayInfoModelMatRes.js`, present locally
in this repo): that column is only ever used by wow.export as an
existence check, then discarded; the real texture source is
`ItemDisplayInfoModelMatRes.MaterialResourcesID`, joined via
`ItemDisplayInfo.ID`, using only `ModelResourcesID_0` (never `_1`, never
an OR across both) for the reverse model→item lookup.

The join was rewritten to match wow.export's algorithm exactly (see
`src/cmd_db2.cpp`'s `model_object_skin_candidates` — every candidate
kept, not collapsed to one, since one model legitimately maps to several
`ItemDisplayInfoID`s: real recolor/seasonal variants sharing one base
mesh). **This did not fix the problem.** Re-checked against
`helm_leather_pvpdruid_b_02_scm.m2` (FileDataID 294420,
`ModelResourcesID` 6706 — confirmed consistent across all 22+ real
race/gender variants of this exact helm, so the model→`ModelResourcesID`
link itself is solid): every one of the 5 `ItemDisplayInfoID`s sharing
that `ModelResourcesID` resolves, via the corrected wow.export-matching
join, to a `staff_2h_pandariatradeskill_c_03*` texture — a weapon skin,
not a helmet, and **not even the same item category**. Meanwhile the
real, correct texture (`item/objectcomponents/head/
helm_leather_pvpdruid_b_02.blp`, FileDataID 294402) sits in the same
directory as the model under an unambiguous, obvious name, and is not
reachable from either `ItemDisplayInfo.ModelMaterialResourcesID_0/1` or
`ItemDisplayInfoModelMatRes` for this `ModelResourcesID` at all.

Root cause, as far as verified: `ItemDisplayInfoID` 113510 (one of the
5) is internally self-contradictory — its own `ModelResourcesID_0`
claims the helm mesh, but its own linked material data points at an
unrelated staff texture, with no deprecated/hidden flag set to explain
it. Most likely orphaned/stale game data (WoW DB2 tables are known to
retain unused/retired records) that a *real* item never actually
references. The disambiguator that should filter this out —
`Item`/`ItemAppearance`/`ItemModifiedAppearance`, confirming a real,
current item actually points at a given `ItemDisplayInfoID` — is the
exact chain this session's earlier `EXPLORATION_TODO.md` work judged
"not needed" once the shorter `ModelFileData` path was found. That
judgment is now known wrong: the shorter path is unreliable precisely
because it skips this check. Those three files are also the ones
already found **truncated locally** earlier this session (same failure
shape `texturefiledata.db2` had before its real re-extraction) — so this
can't be verified further with current local data; a casc-tool
re-extraction ask for `item.db2`/`itemappearance.db2`/
`itemmodifiedappearance.db2` is the concrete next step.

Even if that re-extraction lands and confirms the hypothesis, the
severity found here (confidently wrong beats visibly unresolved) means
this DB2 chain should not be trusted as primary without a real
correctness safety net — at minimum, cross-checking a DB2-resolved
candidate against local basename-family presence before trusting it, or
preferring local basename matching outright when both a DB2 candidate
and a local same-family file exist and disagree.

## Canonical location

`/media/luna/work/cache/husk/knowledge.sqlite` — a husk-built artifact
(`husk db2-build`), not user-supplied input, so it belongs under the
shared system cache dir per the usual convention for generated/rebuildable
data — never under the work dir, which is for content a human puts there
directly. Kept in its own `husk/` subdirectory (not loose at the top of
`/media/luna/work/cache/`, which is a large multi-application shared
cache) so husk's own cache entries stay grouped as they accumulate
(this file plus `husk_corpus_scratch/`, an earlier, differently-named
sibling — not renamed in this pass, out of scope). `render_sample_driver.
py`'s `KNOWLEDGE_DB` constant is the one place this path is recorded.

## What's actually built (2026-08-16)

- `husk db2-build --db2-dir --dbd-dir --listfile -o <out.sqlite>`
  (`src/cmd_db2.cpp`) — ingests `ModelFileData`/`ItemDisplayInfo`/
  `TextureFileData` (via the existing `db2-export` machinery,
  `loadOneFile`/`writeFileTable`, reused not reimplemented), a `models`
  table (FileDataID → path, `.m2` only) and a `textures` table
  (FileDataID → path, `.blp`/`.png` only) — both from `--listfile`, one
  shared `ingestListfileTable` helper, not duplicated — the resolved
  `model_object_skin_texture(model_file_data_id, texture_file_data_id)`
  join (real SQL, indexed — an earlier unindexed attempt didn't finish in
  2 minutes; indexed, ~4s for the full local corpus), and a `_meta` table
  stamping each source `.db2`'s size+mtime. Real run: 131,086/72,419/
  214,436 source rows, 133,733 model paths, 843,078 texture paths,
  **54,188 resolved model → object-skin-texture mappings** — broader than
  the old pipeline's 4,220, precisely because it's no longer gated by
  `unfillable_texture_task.py`'s silent partial-resolution bug.
- `husk export --knowledge-db <path>` (`src/cmd_export.cpp`,
  `resolveObjectSkinTextureFromKb`) — resolves the model's own FileDataID
  via the knowledge base's own `models` table (relative to
  `--listfile-root`), looks up `model_object_skin_texture`, then resolves
  that texture's own real path via the `textures` table directly — no
  separate `--listfile` load needed just to embed this one texture
  (verified live: a full export with `--knowledge-db` but no `--listfile`
  at all still embedded all 3 materials). `--object-skin-texture-id`
  still exists and wins if both are given (kept for the existing test +
  manual override use).
- `resolve_object_skin_textures.py` and the stale
  `object_skin_texture_resolution.csv` — deleted (moved to `trash/`), not
  reinstated even though the KB replacing it is currently disabled — the
  Python DB2-join reimplementation was still the wrong direction
  regardless.
- **Correction**: an earlier version of this section claimed
  `helm_leather_pvpdruid_b_02_scm.m2` "resolves and renders correctly."
  **That was never actually checked for content correctness, only that
  something embedded** — real verification (see "Known-wrong, not just
  unverified" above) found it embeds a completely unrelated weapon
  texture. `render_sample_driver.py` no longer passes `--knowledge-db`.

**Not built** (still just design below, deliberately deferred): the
staleness *check* at export time (the `_meta` stamp is written, but `husk
export` doesn't yet compare it against `--db2-dir` and warn);
`model_animations`/`model_skins`/`model_materials` (no consumer needs them
yet, per "abstractions are earned" — the ingestion code is already
table-driven via `kKbSourceTables`, so adding one is a small, mechanical
addition when a real need shows up, not a redesign).

## Problem

Every "resolve X from DB2 for a given model" need so far has grown its own,
direction-specific path:

- `src/chrmodel_db2.hpp`/`.cpp`, `src/chrcustomization_db2.hpp`/`.cpp` — C++,
  in husk, but require the caller to already know the answer (`--char-
  layout-id`, `--customization-choice-ids`) — no model-to-ID derivation.
- `tools/corpus_scan_tasks/resolve_object_skin_textures.py` — Python,
  outside husk, re-implements a DB2 join (`ModelFileData` → `ItemDisplayInfo`
  → `TextureFileData`) from scratch via ad hoc `sqlite3` ATTACHes, sourced
  from a CSV written by an unrelated, older scan.
- `tools/corpus_scan_tasks/unfillable_texture_task.py` — Python, a
  *different* local-file-only resolution pass that happens to gatekeep what
  the script above even sees, and silently excludes any model with a
  *partially* resolved texture set (found live: `helm_leather_pvpdruid_b_02_
  scm.m2` never entered `unfillable_textures_full.csv` at all because one of
  its two texture slots — an unrelated placeholder — happened to resolve,
  even though its real `object_skin` slot never did).
- `render_sample_driver.py` — reads yet another CSV, does a per-file string
  lookup, injects a CLI flag.

Net effect: two independent DB2-join implementations, a silent gatekeeping
bug, and three CSV/flag hops between "husk read the DB2 tables" and "husk
wrote the pixel." Each new "model needs X" question (texture, but also
animation-kind, skin variant, material, bone name) risks growing its own
fifth path the same way.

Root cause: husk currently treats DB2/listfile access as a **per-feature,
per-call cost** (re-read, re-join, re-derive from scratch every time), when
it should be a **one-time ingestion cost** into husk's own, verified store —
consistent with this project's own foreign-data boundary policy
(`~/.claude/CLAUDE.md`: "validate at the boundary... the interior trusts its
inputs, no defensive checks scattered past the boundary"). Right now the
boundary is crossed *inside* the hot path, repeatedly, by different code
each time.

The listfile crossing specifically has already caused one real bug this
project hit directly: Windows line endings (`\r\n`) silently broke a naive
`awk`/CSV split during this session's own corpus quantification, and
`DESIGN.md`'s own module comment for `--listfile` already documents this as
untrusted, foreign, boundary-crossing data (`src/listfile.hpp`/`.cpp`). Every
ad hoc script that re-parses it is a second, unvalidated boundary crossing.

## Proposed architecture

**A single, husk-owned SQLite database** ("the knowledge base," working
name), built once by a new husk subcommand from local DB2 files + a local
listfile snapshot, versioned/rebuilt explicitly (not implicitly regenerated
per export). Everything downstream — `husk export`, `render_sample_driver.
py`, any future "what does this model need" question — queries *this* one
database, never DB2/listfile files directly.

```
 DB2 files (.db2)  ---\
                        >--  husk db2-build  -->  knowledge.sqlite  <-- husk export reads this
 listfile.csv     ---/       (one-time, explicit)      (trusted zone)
```

### Why SQLite, not another CSV

husk already has a real WDC5 parser (`src/db2.hpp`/`.cpp`) and a real
DB2-to-SQLite exporter (`husk db2-export`) — the boundary-crossing/decode
work already exists. The gap isn't "can husk read DB2," it's "does husk
keep what it read." SQLite is also already the export format
(`db2table.cpp`, `husk db2-export`), so this isn't a new dependency, just a
new *permanent* use of one that currently only produces throwaway scratch
files.

### Schema shape (sketch, not final)

Two kinds of tables:

1. **Flat lookup tables**, one hop each, built directly from a single real
   DB2 table (or the listfile) — no cross-table joins baked in yet:
   - `textures(file_data_id PRIMARY KEY, path)` — from the listfile,
     ingested *once* here (CRLF-stripped, normalized, validated at this one
     boundary crossing) rather than re-parsed by every consumer.
   - `models(file_data_id PRIMARY KEY, path)` — same, for `.m2` files.
   - Real per-table DB2 rows husk already has typed readers for or can
     trivially add (`ModelFileData`, `ItemDisplayInfo`, `TextureFileData`,
     `AnimationData`, `CharComponentTextureLayouts`, ...) — ingested as
     close to their real DB2 shape as WoWDBDefs resolves, not pre-joined.
2. **Resolved junction tables**, one join computed and cached at build
   time, each answering exactly one "model needs X" question:
   - `model_object_skin_texture(model_id, texture_id)` — today's chain.
   - `model_animations(model_id, animation_id, kind)` — `kind` real when
     derivable (e.g. from `AnimationData`'s own name field), else an
     explicit `'unknown'`, never a guess — same "unlabeled beats guessed"
     convention `BONE_NAME_DEDUCTION_TODO.md`'s tiers already established.
   - `model_skins`, `model_materials`, etc., grown the same way, on demand,
     each a small, focused join added when a real need shows up — not
     spequlatively built out now (this project's own "abstractions are
     earned" rule).

Every row in a resolved table is either a real, verified answer or absent
— absent means "husk doesn't know," queried the same way a missing row in
any SQL table would be, not a separate error path. This is also where
DB2's real incompleteness (TACT-encrypted sections still genuinely opaque
even after `recordsAvailable()`'s fix, 0-byte/truncated tables, no
canonical bone-naming table at all) becomes visible *once*, at build time,
with a real diagnostic count — not rediscovered per-file by whichever
script happens to touch that table next.

### What replaces what

- `resolve_object_skin_textures.py` → deleted, replaced by a `husk db2-
  build` step plus a query against `model_object_skin_texture`.
- `unfillable_texture_task.py`'s gatekeeping role in that pipeline →
  removed entirely; husk's own export path checks every `type=2`/`fdid==0`
  slot against the knowledge base directly, regardless of whether some
  *other* slot on the same model happened to resolve. (The scan itself may
  still be useful for its original purpose — corpus-wide missing-texture
  auditing — just no longer load-bearing for this feature.)
- `--object-skin-texture-id <fdid>` (today's flag, one ID per invocation)
  → likely becomes `--knowledge-db <path>` (or reuses `--db2-dir` once it
  points at the built database instead of a live directory), with husk
  doing its own `model_id -> texture_id` lookup per export, keyed by the
  model's own FileDataID (itself resolved via the knowledge base's
  `models` table + the input path, not re-derived per call).
- `render_sample_driver.py` passes one flag, unconditionally, for every
  file — no more per-file CSV lookup/conditional flag injection in Python.

## Decisions (2026-08-16, confirmed with Luna)

1. **Location**: local-only, `.gitignore`d, rebuilt on demand via `husk
   db2-build` — same tier as `--db2-dir`/`--listfile` today. Never
   committed.
2. **`--char-layout-id`/`--customization-choice-ids`**: left alone for now.
   `chrmodel_db2.hpp`/`chrcustomization_db2.hpp` keep their current
   load-everything-filter-by-ID shape; not migrated in this pass.
3. **Staleness**: stamped and detected. `husk db2-build` records a
   hash/timestamp of the source DB2 files it ingested; a consumer (`husk
   export` et al.) warns if `--db2-dir`'s current contents look newer than
   what the knowledge base was built from.
4. **Scope**: build the general mechanism now (a reusable, `db2table.hpp`-
   style generic ingestion/join helper), not just the one table — so the
   next resolved-table (animations, materials, ...) is cheap to add.
   First concrete table built on top of it: `model_object_skin_texture`.

## Non-goals (for this design)

- Not a general DB2 ORM — only tables/joins a real husk feature actually
  needs get built, per this project's "abstractions are earned" rule.
- Not a live-updating cache — DB2/listfile data is static once extracted;
  no need for incremental sync, just a rebuild step.
- Doesn't change husk's boundary policy (locally-extracted DB2/listfile
  files only, never live CASC) — this is entirely about *where* the
  boundary crossing happens (once, at build time) and *what* holds the
  result (husk's own verified SQLite, not scattered CSVs), not about
  loosening or tightening what data is in scope at all.
