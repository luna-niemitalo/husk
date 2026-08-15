# TODO: exploring and wiring the missing texture/data links

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was
fixed and when, not this file.

## Goal

Two direct open questions from the 2026-08-16 DB2-encrypted-section fix
(`CLAUDE_HISTORY.md`'s 2026-08-16 entry, `db2::Section::recordsAvailable()`),
neither answered yet:

1. **How many of the 4,733 `replaceable_only` files**
   (`corpus_reports/unfillable_textures_full.csv`, `unfillable_texture_task.py`'s
   scan) **can actually be resolved now**, given `--db2-dir` can read data
   it silently couldn't before?
2. **Does the same fix unlock anything else** — other missing-texture or
   missing-data gaps this project has already catalogued, not just this
   one bucket?

The end goal is not just answering these — it's **wiring the resolved
links up so real textures actually get embedded and rendered**, the same
bar every other fix in this project is held to (a real render, not just
"the data is readable now").

## Why this needs real exploration, not another guess

`unfillable_texture_task.py`'s scan does **not** consult `--db2-dir` at
all — it only checks local `.blp`/`.png` files (literal FileDataID,
`--listfile`, fuzzy same-basename). Its 4,733-file `replaceable_only`
count will not change by re-running that scan; the DB2 fix doesn't touch
anything that scan measures. Answering question 1 requires actually
walking the DB2 chain end to end for real files and checking whether it
resolves to a real, locally-present texture — that hasn't been done yet.

A first look at the chain (2026-08-16, via `husk db2-export` + real SQL,
not guessed) found it has **more hops than assumed**:

- `ItemDisplayInfoMaterialRes` (`ItemDisplayInfoID`, `ComponentSection` →
  `MaterialResourcesID`) and `ItemDisplayInfoModelMatRes`
  (`ItemDisplayInfoID` → `MaterialResourcesID`, `TextureType`,
  `ModelIndex`) don't key by the model's own FileDataID directly, and
  neither resolves straight to a texture FileDataID.
- No standalone `MaterialResources.db2` exists locally. `componenttexturefiledata.db2`
  and `texturefiledata.db2` both exist and are plausible next hops
  (`MaterialResourcesID` → `ComponentTextureFileData` keyed by
  `(ID=MaterialResourcesID, ComponentSection)` → a real FileDataID is the
  standard retail-client shape for this chain, per general DB2 naming
  convention — **not yet confirmed against real rows**, don't trust this
  without checking).
- Getting from one of the 4,733 `.m2` files to an `ItemDisplayInfoID` in
  the first place is its own open sub-problem — probably
  `ItemModifiedAppearance`/`ItemAppearance` (both present locally,
  unexplored) or a direct `ItemDisplayInfoModelMatRes` reverse-lookup by
  `ModelFileDataID` if such a column exists somewhere in the chain (not
  seen yet in the tables checked so far).

## Exploration plan

1. **Audit every existing husk DB2 consumer for the same
   `recordsAvailable()` blast radius.** `src/chrmodel_db2.hpp`/`.cpp`,
   `src/chrcustomization_db2.hpp`/`.cpp`, and any other `db2table.hpp`-
   based reader may have been silently working with incomplete data
   before this fix (not just the two tables checked in the 2026-08-16
   investigation). Re-run each against the real corpus and diff row
   counts / previously-empty results against post-fix output.
2. **Map the real `.m2` → `ItemDisplayInfoID` chain.** Check
   `ItemModifiedAppearance.db2`/`ItemAppearance.db2`/`Item.db2` (all
   present locally) via `husk db2-export --dbd-dir reference/WoWDBDefs`
   for real column names, and confirm which one actually carries a
   model-identifying FileDataID that a loose corpus `.m2` file (no
   ItemID metadata of its own) can be matched against.
3. **Map `MaterialResourcesID` → real texture FileDataID.** Check
   `componenttexturefiledata.db2`/`texturefiledata.db2`'s real schemas
   and confirm the join with real rows (pick a `MaterialResourcesID` from
   `ItemDisplayInfoMaterialRes`'s own real data, verify it resolves).
4. **Prototype against real files.** Pick a handful from the 4,733
   bucket (the `chest_mail_raidevokergoblin_d_01_*` race/gender family
   used throughout the 2026-08-15/16 investigation is a good known-shape
   starting point) and walk the full chain by hand via SQL against a
   real `db2-export` dump, confirming an actual resolvable FileDataID
   comes out the other end — and that the corresponding `.blp`/`.png`
   actually exists locally (a DB2-resolved FileDataID can still hit the
   same "genuinely missing, needs re-extraction" wall the texture-side
   investigation already mapped once this session).
5. **Quantify.** Once the chain is confirmed correct on a sample, run it
   across all 4,733 files (or as many as the chain's own preconditions
   allow — some may lack an `ItemDisplayInfoID` mapping at all) for a
   real resolved/unresolved count. Answers question 1.
6. **Check `character/`'s 144 excluded files too.** They're currently
   assumed to use a wholly separate mechanism (character skin
   compositing via `ChrModel`/`ChrCustomizationOption`, not
   `ItemDisplayInfo`) and structurally blocked by `chrcustomization*.db2`'s
   genuine 0-byte tables (`CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 3) —
   confirm this is still true post-fix, don't assume.
7. **Wire it up for real.** Once the chain is understood and quantified,
   implement it as an actual husk feature — most likely extending
   `--db2-dir` to auto-derive the right texture per model instead of
   requiring a caller-supplied `--char-layout-id` (today's real
   limitation, `DESIGN.md`'s CLI table), or at minimum a corpus-wide
   preprocessing script that resolves and records the right ID per file
   so `tools/full_render.py` can pick it up for a real re-render.
8. **Re-render.** Once wired, re-run `tools/full_render.py` against
   whatever fraction of the 4,733 (and possibly some of the 144
   `character/` files) turns out resolvable, and confirm real textured
   output — the actual bar for calling this done, not just "DB2 data
   resolves."

## Non-goals (for this file)

- Full pixel compositing (blending multiple resolved texture layers
  together) is `CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 4 — a later,
  separate concern. This file's goal is just: find the real FileDataID
  link and get *a* real texture embedded, not blend several correctly.
- `chrcustomization*.db2`'s 0-byte tables (Stage 3) are a known,
  separate `casc-tool` re-extraction gap, not something to work around
  here.
