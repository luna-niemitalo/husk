# Note for whoever picks up world/doodad M2 texture handling

**Status: a scratch note, not a tracked TODO.** Fold this into whichever
real `WORLD_*.md`/`*_TODO.md` doc actually owns doodad-texture resolution
once that work starts, then delete this file — same throwaway-scratch-doc
lifecycle `DESIGN_CHANGES.md` and the old `VERIFICATION_IDEAS.md` had (see
`CLAUDE.md`'s Resume section).

## What this is about

A full local-corpus scan (`tools/find_texture_type_collisions.py` +
`tools/corpus_scan_framework.py`, 130,576 real `.m2` files,
`TEXTURE_TYPE_COLLISIONS_REPORT.md` has the full writeup) found that
**51.2% of the corpus (66,847 files) has more than one `M2Texture` record
sharing the same `type` value**, and the collision is concentrated almost
entirely outside `character/`:

| top-level dir | affected files |
|---|---|
| `world` | 30,368 |
| `item` | 22,931 |
| `spells` | 8,753 |
| `creature` | 2,924 |
| `character` | 89 |

`world`+`item`+`spells` alone are 93% of every collision found. This is
exactly the population `ADT`'s `MDDF`/`MODF` doodad placement (see
`WORLD_COMPLETENESS.md`'s "Doodad (M2) placement onto a tile" row) will
pull M2s from once that lands — so real-world doodad texture handling will
run into this constantly, at a much higher rate than character work ever
does (character was checked and found irrelevant to this, see below).

## The actual gotcha, for whoever writes doodad texture resolution

Every one of these 66,847 collisions is on `M2Texture::type == 0`
(hardcoded/embedded), never a named replaceable slot. `husk::m2::Header`'s
`textureLookup` array (wowdev.wiki's "Replacable texture lookup" -- a
reverse `type -> texture index` map) picks exactly **one** texture index
per type -- "just the last one written to the file" per the wiki's own
words, not a meaningful client choice. When a real batch's own
`textureCombos` chain (the actual, deterministic per-material resolution
husk's `cmd_export.cpp` already uses) is checked against what
`textureLookup` would have picked instead, **84.7% of the time they
disagree** (140,557 of 165,968 batches that touch a colliding type).

**The one safe rule: never resolve a doodad's texture by `type` alone.**
`textureLookup` (or any `type`-keyed shortcut) is not a reliable substitute
for the real chain (`skin batch -> textureComboIndex -> M2's own
textureCombos -> a specific texture array index -> that exact texture's
own FileDataID/filename`) -- husk's existing M2 material-resolution code
already does this correctly and should be reused as-is for doodads, not
reimplemented with a `type`-based shortcut that looks equivalent but
silently picks the wrong texture better than 5 times out of 6 when it
actually matters.

## Why this doesn't affect husk today

Checked directly (not assumed): `export_materials.cpp`'s per-batch
resolution already goes through the real chain above, keyed by a specific,
already-resolved texture array index -- it never consults `type` to *pick*
a texture, only to categorize one it already has (for the unrelated
fuzzy-disk-file-candidate-pool problem, itself keyed off a specific
texture's own unresolved FileDataID, not off `type`-collisions). So this
finding is real and well-quantified, but inert for character models (only
89 files, and even those already resolve correctly) and for husk's current
M2-only pipeline generally. It becomes relevant the moment any future code
path tries to shortcut through `type`/`textureLookup` instead of the real
per-batch chain -- which is exactly the doodad-placement case this note is
for.

## Where the data lives

- `texture_type_collisions.csv` (repo root, untracked scratch output --
  same convention as `m2_chunk_discovery.csv`/`*_files_for_exploration.txt`,
  not gitignored, just not meant to be committed) -- one row per affected
  file.
- `TEXTURE_TYPE_COLLISIONS_REPORT.md` -- full reasoning/results writeup.
- `tools/find_texture_type_collisions.py` / `tools/corpus_scan_framework.py`
  -- rerunnable if the corpus changes.
