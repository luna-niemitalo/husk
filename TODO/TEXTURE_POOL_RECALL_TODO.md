# TEXTURE_POOL_RECALL_TODO.md — the fuzzy pool excludes 84% of the right answers

Open punch list. Fixed items get removed outright; git history is the record.

## The finding

`scanFuzzyTexturePool` builds its candidate set with
`stem.startswith(model_basename)`. Measured against ground truth (the 415
DB2-named textures actually on disk for `bloodelffemale_hd`, out of 1029 `.blp`
in `character/bloodelf/female/`):

| pool rule | pool size | contains truth |
|---|---|---|
| `startswith(basename)` — today | 94 | **66/415 = 15.9%** |
| any semantic filename tag | 1016 | **409/415 = 98.6%** |
| tag **and** `_hd` | 467 | 373/415 = 89.9% |

**No ranking change can fix this.** `orderCandidatesForDefault`'s four measured
signals are choosing among a set that excludes 84% of the correct files. Every
"character texture looks wrong" symptom traces here, and it is why tuning the
ranking has never fixed it.

## Why the rule fails

`character/bloodelf/female/` alone uses at least four naming conventions:

```
bloodelffemalenakedtorsoskin00_105_hd.blp   no separators in the main name
scalpupperhair00_10_hd.blp                  no race/sex prefix at all
bloodelf_female_dh_tattoo_30_e.blp          underscore-separated race/sex
tempmonktexture.blp                         ???
```

`startswith("bloodelffemale_hd")` matches only the first form. It also splits
badly across the HD/non-HD divide, in opposite directions:

- `bloodelffemale_hd.m2` → pool of **94 / 1029** — starved.
- `bloodelffemale.m2` → pool of 934, of which **463 (49.6%) contain `_hd`** and
  do not apply — poisoned. `orderCandidatesForDefault` ranks partly by decoded
  pixel area and has no `_hd` awareness, so it actively *prefers* the wrong,
  higher-resolution file. Not a coin flip; a biased one.

Claim-and-remove compounds both: a wrong claim permanently deletes that
candidate for every later slot on the model.

## What the fix is

**DB2 is the character path; tags are the degradation path.** For
`bloodelffemale_hd` the DB2 chain names 440 FileDataIDs and 432 resolve
(98.2%): 415 in the model's own directory, 17 one level up (tier 4), 8
genuinely missing. That is resolution by ID with no naming guesswork — it
should be primary for characters, not a parallel option.

Tags matter for when it *fails*: a 0-byte DB2 table is a documented recurring
reality here (`texturefiledata.db2`, `chrcustomization*.db2`). Falling back to
"files tagged `tattoo`" degrades far better than "files starting with the model
name" — a wrong tattoo beats a face texture on the eyeballs.

## Tags are a filter set, not a classification

A file carries a *set* of tokens; a query is a **conjunction** of the tokens
implied by what is being looked for. One tag per texture type does not work,
because the coarse tokens are shared across disjoint subcategories — measured
in `character/bloodelf/female/`:

| query | pool |
|---|---|
| `skin` | 154 |
| `skin` + `color` | **12** |
| `skin` + `pelvis` | 40 |
| `skin` + `torso` | 40 |
| `skin` + `naked` | 80 (= pelvis 40 + torso 40) |
| `hair` + `color` | 5 |
| `scalp` + `upper` | 14 (`scalp` + `lower` = 0 — scalp is always upper) |
| `tattoo` + `_e` | 36 of 72 (clean binary variant axis) |

So `naked` is a parent of `torso`/`pelvis`, and `color` is an orthogonal axis
crossing them. A single tag is a coarse gate; the intersection is the query.
The query's tokens come from two places husk already has: the slot's own
texture type, and (with DB2) the `ChrCustomizationOption` name — "Skin Color"
yields `skin` + `color`, which is exactly the 12-file pool.

## HD/non-HD is a hard partition, not a preference

389 of the 415 ground-truth files carry `_hd`. There are 210 `facelower` files
in the folder, **none** `_hd`, and **none** in the HD model's ground truth —
`facelower` is a non-HD-only concept. Ground truth by token: 246 faceupper,
0 facelower, 60 skin, 36 tattoo, 30 naked, 29 hair, 19 eye, 13 scalp, 2 jewel.

So `_hd` filtering for an `_hd` model is more correct than the 98.6% → 89.9%
recall figure suggests: most of what it drops is art that model genuinely does
not use. Do not treat the recall cost as pure loss without checking which
files it actually removes.

## Steps

1. **Derive the tag vocabulary from the corpus, not by hand.** The prototype's
   tag map was eyeballed off one folder and is known incomplete (`object_skin`
   returned 0 candidates because those textures live with items). Do it the way
   `stripRaceGenderSuffix`'s race codes were derived: frequency-count tokens
   across a real extraction, keep only high-occurrence ones, record the counts.
   Derive *co-occurrence* too, not just frequency — the parent/child and
   orthogonal-axis structure above is what makes conjunctions work, and it is
   not guessable from a token list alone.
2. **Make `Catalog::texture()` prefer the DB2-resolved FileDataID for character
   models** before any pool tier runs.
3. **Replace the pool's `startswith` gate with a tag-conjunction query**, keeping the
   existing per-type filter. Expect pool sizes to *grow* for broad tags (face
   750, skin 139) and shrink for narrow ones (hair 27, eyes 13, jewelry 8) —
   size is the wrong metric, recall is the right one.
4. **Drop claim-and-remove.** With tag gating the "one image can't fill every
   slot" property comes from type compatibility instead, and the failure mode it
   causes today (permanent deletion from an already-starved pool) is worse than
   the problem it solves.
5. **Apply `_hd` discipline per model variant**, not globally — see the
   partition section above: the headline 98.6% → 89.9% recall cost is mostly
   art the `_hd` model genuinely does not use, and it removes the base model's
   49.6% false-positive rate.
6. **Re-run the resolution-ledger diff** (`husk export --explain-textures` /
   `husk resolve`, `REFACTOR/README.md`'s stage-1 gate). Every delta attributed;
   deltas here are expected and are the point.

## Gate

Independent to implement and measure. The final "does this character look right
in Blender" call is Luna's, as always.

## Reproducing the numbers

Ground truth = DB2-named FileDataIDs for the model, resolved through
`--listfile` to real paths, intersected with what is on disk. Note the
community listfile is **CRLF** — an unstripped `\r` silently makes every path
miss, which cost this investigation one wrong "0 files exist" conclusion before
it was caught.
