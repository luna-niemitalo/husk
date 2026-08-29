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

## Step 1 findings (2026-08-29): vocabulary + co-occurrence derived, corpus-wide

Script: `tools/derive_texture_tag_vocabulary.py`. Artifact:
`corpus_reports/texture_tag_vocabulary.json` (1849-word vocabulary, 1213
tokens reported at the `--report-min-count 200` threshold, 96,705
co-occurrence pairs; JSON not CSV — the two tables (per-token per-directory
frequency, per-pair conditional-probability/relation records) are
structurally different and deeply nested, and the method/thresholds live
alongside the data they produced in one self-describing file rather than a
convention linking 2+ separate CSVs).

**The artifact is deliberately not committed** — 19.6 MB, of which the
co-occurrence table alone is 16.2 MB; regenerable from the script; and
`/corpus_reports/` is gitignored repo-wide. Regenerate with:

```
direnv exec . tools/venv/bin/python tools/derive_texture_tag_vocabulary.py
```

Every number this section relies on is quoted inline below, so the findings
stand without the file. When step 3 needs a durable token list in the
binary it should become a real source-level table with its counts in a
comment — the way `stripRaceGenderSuffix`'s race codes already are — not a
checked-in blob.

**Method**: two-phase, not a single `split("_")` pass. (1) Seed a
vocabulary from atoms already standalone somewhere in the 771,548-file
corpus (real cross-file frequency ≥100, same "hundreds of real
occurrences" bar `stripRaceGenderSuffix`'s race codes used). (2)
Iteratively re-segment every filename against the current vocabulary,
mine the leftover unmatched spans as new-round candidates, and retire a
coarser already-accepted word once a finer decomposition of it becomes
available (crediting the finer pieces with the coarser word's own file
count) — 12 rounds to converge, stable from round 8 on. A first attempt
(mining arbitrary frequent substrings directly, weighted by file count)
was tried and **failed** real validation outright — `naked`/`pelvis`/
`torso`/`scalp+upper` all came back 0 — root-caused and abandoned; the
full account (two compounding bugs: file-count-weighted windows tying
against real word boundaries, then even doc-frequency-weighted mining
being mathematically biased toward the shortest possible fragment) is in
the script's own `ITERATE_RESIDUAL_MINING` comment, not repeated here.

**Validation against this doc's own hand counts** (`character/bloodelf/
female/`, 1029 files): 8 of 9 reproduced exactly — `skin+color` 12,
`skin+pelvis` 40, `skin+torso` 40, `naked` 80, `hair+color` 5,
`scalp+upper` 14, `scalp+lower` 0, `tattoo` 72. The 9th, `skin` = 154,
came back 139 — reconciled as **this doc's own hand-tally error**, not a
tokenizer bug: 139 is an exact partition (47 bare `..skin00_*` + 40
`nakedpelvisskin` + 40 `nakedtorsoskin` + 12 `skin_color` = 139),
confirmed three independent ways (shell `grep -c`, Python substring
count, this script), and 154 has no matching breakdown.

**Corpus-wide counts for this doc's named tokens** (vs. the single-folder
numbers above): `skin` 6851, `color` 5100, `hair` 7129, `naked` 1950
(character 1941, creature 8, world 1), `torso` 946, `pelvis` 1060 (100%
character/), `scalp` 2391, `upper` 12888, `lower` 7282, `tattoo` 702,
`face` 10803, `eye` 408, `jewelry` 959.

**Structural relations, confirmed at full-corpus scale** (the single-folder
versions above were bloodelf/female-only):

- `pelvis` ⟹ `naked` 100% of the time (1060/1060); `torso` ⟹ `naked` only
  93.1% (881/946) — **not** the same relation. The 65-file gap is
  `torso` used independently by item/armor texture-component naming
  (`item` 30 + `creature` 25 + `interface` 3 + `world` 6 = 64, matches),
  which `pelvis` never is. `torso` is a broader term than `pelvis` in
  this corpus; do not assume symmetric siblings without checking.
- Within character/: `naked` = `torso` ∪ `pelvis`, exactly — 881 + 1060 =
  1941 = `naked`'s own character/ count. Confirmed directly (not
  inferred from absence) that `torso`+`pelvis` never co-occur in the
  same file, corpus-wide, and neither do `upper`+`lower` — both genuine
  disjoint-sibling pairs, same shape as `scalp+lower = 0` above.
- `scalp+upper` 1370, `scalp+lower` 959, sum 2329 vs. `scalp`'s own total
  2391 — 62 files (2.6%) unaccounted for, not investigated further.
- `color` is orthogonal, not implied or implying: `skin+color` 723
  (P(color|skin)=0.14), `hair+color` 567 (P(color|hair)=0.11), lift
  ~12-16x above independence but nowhere near the ≥0.95 conditional-
  probability bar used for `a_implies_b`/`b_implies_a` classification —
  consistent with `color` being a customization-choice axis that applies
  to a minority of files, crossing multiple base tags, not a subtype of
  any one of them.
- `naked` ⟹ `skin` 100% (1950/1950) and, restricted to files that carry
  both, the relation is symmetric-in-practice for the naked-body
  convention specifically (every `naked` file is also a `skin` file in
  this corpus).

**The `object_skin` gap named above is real and confirmed unfixable by
filename tokens, any method.** `object_skin`/`objectskin` occurs in **zero**
real filenames anywhere in the 771,548-file corpus (checked directly) —
it was never a filename convention. `object` alone occurs 39,485 times,
almost entirely from the `item/objectcomponents/` **directory path**, not
the file stem this script (by the step's own stated scope) tokenizes.
`object_skin` is a husk/M2 texture-*type* enum name
(`m2::textureTypeName`); item textures are named after the equipped piece,
not the semantic slot. If step 3 still wants this tag, it needs
directory-path tokens or the M2 texture-type field itself, not more
filename mining.

**Known limitations, stated plainly**:
- A word that never appears in a different flanking context anywhere in
  the corpus — not even after the outer prefix/suffix layers are
  stripped — is statistically indistinguishable from noise; nothing here
  recovers it. Not hit among this doc's own named tokens (all recovered),
  but the limit is real for anything not checked this session.
- One real mis-segmentation found and left as-is: `bakednpctextures/
  creaturedisplayextra-<id>[_hd].blp` (81,983 files, ~10.6% of the whole
  corpus — a single mechanically-generated naming template) segments to
  `crea`+`ture`+`display`+`extra` instead of `creature`+`display`+`extra`,
  because two unrelated 4-char fragments happen to exactly tile
  `creature` with zero leftover, passing the residual-length safety
  check that catches partial-garbage cases (guards against, e.g.,
  `female` → `male`+`fe`) but not exact zero-residual tiling. This is in
  `textures/`, not `character/`; not fixed, since fixing it risks
  regressing the now-passing character/ validation for a directory
  unrelated to this doc's actual problem.
- The co-occurrence table only records pairs with `count_both > 0`
  (sparse). A genuinely always-disjoint pair (`torso`+`pelvis`,
  `upper`+`lower`) is represented by *absence*, not an explicit zero row
  — confirmed directly for both pairs above, not just inferred.
- Corpus-wide token rank is dominated by non-character directories
  (`world`/`interface`/`item`/`textures` combined dwarf `character`'s
  32,754 files) — a consumer deriving character-specific tags must read
  the `by_dir` breakdown, not the raw corpus-wide count. (This is why
  `--cooccur-top-k` defaults to 5000, not a smaller number: a first pass
  at 150 silently dropped every token named in this doc, because they
  ranked below tens of thousands of cross-domain files.)

## Step 2 investigation (2026-08-29): the DB2-preferred tier is type-conditional, not universal

Investigation-only pass, no production code touched. Full commands in
"Reproducing the step-2 numbers" below. Everything here is **real data**
(`husk export --db2-dir/--dbd-dir` against the real local
`bloodelffemale_hd.m2`/`chrmodeltexturelayer.db2` extraction, real `--dbd-dir`
schema, real chr_texture_layout/chr_customization_options extras read back
out of the produced `.glb`) unless marked **wiki** (a hypothesis, not yet
independently confirmed for the specific case cited).

**Q1 — is there a real mapping from M2 `textureType` to `ChrModelTextureTargetID`?**
Yes, but it is two separate facts, not one join:

- M2's own hardcoded texture-type numbering (`m2_header.cpp`'s `textureTypeName`,
  e.g. 1=skin, 6=char_hair, 9=ui_skin, 19=char_eyes, 20=char_jewelry) **is the
  same enum space** as `ChrModelMaterial.TextureType` /
  `ChrModelTextureLayer.TextureType`. Confirmed three ways: (a) real —
  `reference/WoWDBDefs/definitions/ChrModelTextureLayer.dbd` declares
  `TextureType` and `ChrModelTextureTargetID<32>[2]` as two distinct real
  columns, matching `chrmodel_db2.hpp`'s own claim that they're separate
  fields, not the same one read two ways; (b) **wiki** —
  `documentation/wowdev-wiki/md/Character_Customization.md` names the overlay
  layers "6, 8, 10, 19, 20, 21, 22, 24", exactly M2's own char_hair/
  skin_extra/tauren_mane/char_eyes/char_jewelry/char_secondary_skin/
  char_secondary_hair/[unnamed 24] numbering; (c) real — `bloodelffemale_hd`'s
  own exported `chr_texture_layout.materials` carries `texture_type` values
  `{1, 6, 9, 19, 20}`, exactly the 5 non-object_skin hardcoded M2 slots this
  model's own `husk resolve`/`--explain-textures` ledger shows with `fdid=0`
  (type 2/object_skin is the 6th hardcoded slot on this model and correctly
  has **no** `ChrModelMaterial` row at all — real, matches the wiki's own
  "Object Skin -- Item, Capes" description, i.e. genuinely outside the
  character-atlas system, not a gap).
- `ChrModelTextureTargetID` is a **separate join key**, not derivable from
  `textureType` alone — it identifies one specific layer *within* a
  `textureType` group. Cardinality is **1:N, N real and layout-specific**, not
  1:1. Measured two ways: per-model (`bloodelffemale_hd`'s own layout 122):
  type 1 (skin) → 13 distinct targets, type 6 (hair) → 1, type 9 (ui_skin) → 1,
  type 19 (eyes) → 2, type 20 (jewelry) → 1. Corpus-wide (all 928 real rows of
  `chrmodeltexturelayer.db2`, ~74 distinct real `CharComponentTextureLayoutsID`
  layouts, no model filter): type 1's layer count per layout ranges 1–14
  (mean 7.9; only 11/74 layouts, 14.9%, have exactly 1); type 6 ranges 1–2
  (mean 1.07); type 19 ranges 1–2 (mean 1.95); type 20 ranges 1–2 (mean 1.09).
  **So "N==1" must be checked per (`CharComponentTextureLayoutsID`,
  `textureType`) at runtime from already-loaded `chrmodel::Data` — it is not
  implied by the M2 `textureType` value alone**, even though 6/9/20 happen to
  be single-layer for *this* model.
- `ChrModelTextureTargetID` is **not** globally unique across `textureType`s
  in the whole table (10 target IDs are each reused by 2–5 different
  `textureType`s corpus-wide — real, measured via `sqlite3` on the full
  `db2-export`) — but **is** unique within one
  (`CharComponentTextureLayoutsID`, target) pair in 927/928 real rows (one
  real exception found: layout 195, target 2, spans 3 distinct
  `textureType`s — not investigated further). Any join must stay scoped by
  the model's own real `CharComponentTextureLayoutsID`, matching how
  `chr_texture_layout` is already structured today.

**Q2 — is a DB2-preferred tier even the right shape for type 1 (skin)?** No,
confirmed by real data, not just the concern's own reasoning. The wiki
itself says character skins are "several layers" with an explicit unfinished
TODO for "how to compile base layer texture as well as how to overlay layer
> 1 textures" — a hypothesis until checked. Checked: `bloodelffemale_hd`'s
own `chr_texture_layout` has 13 real `texture_layers` for `texture_type=1`,
fed by **at least 5 independently selectable `ChrCustomizationOption`s**
(Skin Color → targets 1/13/14, Face → target 5, Hair Style → target 12,
Tattoo Color → target 16, Bracelets → target 26), each contributing its own
real FileDataID to a distinct pixel region — `chr_texture_layout.sections`
gives real non-overlapping placement rects tiling one shared 2048×1024 atlas
(confirmed: the first 4 sections tile the atlas's top-left quadrant with
real disjoint x/y/width/height). There is no single fdid to prefer here —
"prefer the DB2 fdid" is not expressible for type 1 on this model, and (per
the corpus-wide count above) not on 85% of real layouts either. This is the
one part of the concern in the task brief that the investigation fully
confirms: the premise "one fdid per hardcoded slot" is wrong for skin.

**Q3 — which M2 texture types get a clean single-fdid DB2 answer?** None of
them unconditionally — even the 3 single-layer types found for this model
(6/hair, 9/ui_skin, 20/jewelry) have real failure modes, found by actually
resolving default choices, not assumed:

| type | target | default choice (OrderIndex 0) | result |
|---|---|---|---|
| 20 char_jewelry | 38 | fdid 3613861 | clean hit — **matches** what the fuzzy pool arbitrarily picked (coincidence, not causation: the pool has no notion of OrderIndex) |
| 19 char_eyes | 25 | fdid 3492879 (`eyes00_00_3492879`) | clean hit — **differs** from the fuzzy pool's arbitrary pick (fdid 3608322) — a real case where a DB2 tier changes the answer, target 44 (the type's 2nd layer) has no feeding option in this model at all |
| 6 char_hair | 10 | choice 1801, `materials: null` | **no usable fdid** — the real default choice for Hair Color carries no material row for target 10 at all; a DB2 tier must fall through to the pool here, not replace it |
| 9 ui_skin | 40 | choice 1857 "None", `materials: null` | **no usable fdid** — real default is deliberately no blindfold texture; the pool's arbitrary pick (fdid 7758260, a real "Flame" blindfold) is objectively wrong relative to the true default, but a DB2 tier that *only* tries the default choice also produces nothing here, not a fix by itself — a non-default "Flame" choice for the same target carries *two* material rows disambiguated only by `related_choice_id` against a second, unidentified option, a real conditional-material case out of this pass's scope to resolve further |

**Q4 — where does the tier go, replace or precede the pool?** Precede
(fall through), never replace outright — the Hair Color/Blindfold gaps above
are proof a hard "replace" breaks real cases this session actually hit. The
tier is:

- **Type-conditional**: fires only for a (this model's real
  `CharComponentTextureLayoutsID`, this slot's `textureType`) pair with
  exactly one live `ChrModelTextureLayer` row — a runtime count against
  already-loaded `chrmodel::Data`, never a hardcoded type list (type 6/9/20
  being single-layer is a fact about *this* layout, not a fact about those
  type numbers in general — see Q1's corpus-wide range).
- **Choice-dependent**: needs a resolved `ChrCustomizationChoiceID` for
  whichever `ChrCustomizationOption` feeds that one target — explicit
  `--customization-choice-ids`, or the existing
  `defaultChoiceIdsForModel` heuristic (already documented as husk's own
  guess, not client-authoritative).
- **Best-effort**: on a miss (no live single-layer target, no resolved
  choice, or — as found above — a resolved choice with no material row for
  that target), falls through to tier 3 exactly like tier 1/2 already do.
- **Ranked between tier 2 (listfile) and tier 3 (fuzzy pool)** when it does
  fire: it is exactly as deterministic as tier 2 (a real, named, chosen
  answer, not a guess) once the two conditions above hold, so by
  `RESOURCE_CATALOG.md`'s own "deterministic outranks tier 3" logic it
  belongs directly after tier 2, not folded into tier 3 and not a full new
  top-level tier either.
- **New plumbing, not a one-line change**: `Catalog::texture()` today takes
  only `(fdid, textureType, TextureModelContext)` — no DB2 handle at all.
  This tier needs an optional injected `chrcustomization::Data` +
  `chrmodel::Data` + the resolved choice-ID map, present only when the
  caller has them (`cmd_export.cpp` already loads all three for the existing
  `--customization-choice-ids`/`--chr-model-id` flow) — a real new
  constructor parameter / setter on `Catalog`, not something step 3's
  tag-conjunction work touches.

**Not determined this pass** (flagging rather than guessing):
- Whether type 8 (skin_extra)/21/22/23 (secondary skin/hair/armor)/24
  (unnamed) follow the same textureType↔TextureType correspondence — plausible
  by the same wiki citation, but `bloodelffemale_hd`'s own layout uses none of
  them, so this session found zero real rows to confirm those specific types.
- Why layout 195 has one target ID spanning 3 texture types — not
  investigated (which race/model it belongs to, whether it's a real
  exception or a `resolveFieldString`-class data-decode artifact).
- How common the Blindfold-style `related_choice_id`-conditioned multi-material
  case is corpus-wide (rare edge case vs. common pattern) — would need a
  corpus-wide query, out of this investigation-only pass's scope.
- Whether the Hair Color/Blindfold "default choice has no material" gaps
  found on `bloodelffemale_hd` are common across races/models or specific to
  blood elf — only one real model was resolved end to end this pass.

### Reproducing the step-2 numbers

```
husk export bloodelffemale_hd.m2 --output out.glb --explain-textures
  # (config.toml already supplies --db2-dir/--dbd-dir/--listfile for this
  # machine — see ~/.config/husk/config.toml)
  # ledger lines of the form "slot N fdid=0 type=T -> fuzzy-same-basename-pool"
  # are the hardcoded-slot rows this investigation is about.
```
Then, on the produced `.glb`: find the root-joint node whose `extras` carries
`chr_texture_layout`/`chr_customization_options` (per-glb, no fixed node
index — see `CLAUDE.md`'s "skin extras -> root-joint-extras migration"
entry), and inspect `.materials`/`.texture_layers`/`.sections`/
`.customizationOptions[].choices[].materials` directly (`jq` over the
extracted JSON chunk works fine — no husk-side tooling needed for this
one-off check).

For the corpus-wide `ChrModelTextureTargetID` cardinality numbers:
```
husk db2-export --dbd-dir reference/WoWDBDefs \
  /media/luna/data/wow_export/dbfilesclient/chrmodeltexturelayer.db2 out.sqlite
sqlite3 out.sqlite "SELECT TextureType, MIN(cnt), MAX(cnt), AVG(cnt) FROM
  (SELECT CharComponentTextureLayoutsID, TextureType, COUNT(*) cnt
   FROM ChrModelTextureLayer GROUP BY CharComponentTextureLayoutsID, TextureType)
  GROUP BY TextureType ORDER BY TextureType;"
```

## Step 3/5 findings (2026-08-29): tag-conjunction pool + `_hd` hard partition, real deltas

Implemented as two changes, both in `src/export_texture_resolution.{hpp,cpp}`/
`src/sources/catalog.cpp`, claim-and-remove (step 4, still open above)
untouched:

- **Pool admission** (`scanFuzzyTexturePoolForBasename`): a candidate is now
  admitted when it either starts with the model's basename (the original
  rule, kept — the only real signal for files with no vocabulary tag at all,
  e.g. creature recolor names like `gnoll2_armor_brown.blp`) **or** carries
  any real `kTextureTagVocabulary` token anywhere in its stem (substring, not
  a delimited-token split — real ground-truth names run tags together with
  no separator, e.g. `nakedtorsoskin`). Every admitted candidate must then
  also match the model's own real `_hd`-ness (`filenameCarriesHdToken`,
  delimited-token, not substring) — step 5's hard partition, enforced once at
  the pool itself so every downstream consumer (the per-type query below,
  the older category filter, `orderCandidatesForDefault`'s ranking) inherits
  it for free.
- **Per-type query** (`filterCandidatesByTextureTag`, new,
  `textureTypeTagClauses`): an OR-of-AND-clause table for the texture types a
  confident real-corpus tag exists for — `{1,8}` (skin/skin_extra): OR of
  `{skin}`/`{face}`/`{tattoo}`/`{naked}` (a real composite slot, confirmed by
  step 2's own Q2 investigation — several independent options feed it at
  once); `{6,22}` (char_hair): `{hair}` alone, deliberately not `+color`,
  since real Hair *Style* files carry no "color" token; `{19}` (char_eyes):
  `{eye}`; `{20}` (char_jewelry): `{jewelry}` **and** `{color}` together,
  narrower than a bare `jewelry` tag on purpose — see `candidateCategoryTypes`'
  own doc comment for why `body_jewelry`/`bracelets` (a skin overlay) must
  stay excluded from a real jewelry-mesh texture's own candidate set, which a
  bare `jewelry` substring can't tell apart but `jewelry`+`color` together
  can (reproduces the pre-existing 2-file `bloodelffemale_hd` answer exactly).
  Every other type (object_skin(2) most notably — unreachable by any
  filename tag, see above — plus every weapon/environment/monster
  replaceable type) returns `nullopt` and falls back to the older,
  byte-for-byte-unchanged `filterCandidatesForType`.

**Baked vocabulary** (`kTextureTagVocabulary`, 13 tokens): reuses the exact
corpus-wide counts this doc's own step-1 findings section above already
quotes (`skin` 6851, `color` 5100, `hair` 7129, `naked` 1950, `torso` 946,
`pelvis` 1060, `scalp` 2391, `upper` 12888, `lower` 7282, `tattoo` 702,
`face` 10803, `eye` 408, `jewelry` 959) — no need to regenerate the 19.6 MB
artifact for numbers already on record. A from-scratch Python replica of the
admission/query logic above, run against the real
`character/bloodelf/female/` folder (1029 files) and the model's own real
ground truth (440 DB2-named FileDataIDs from `bloodelffemale_hd`'s own
`chr_customization_options` extras, 415 present in the folder — reproduced
directly from a real exported `.glb`, not re-derived by hand), reproduced
every number this doc's earlier sections already state exactly (`skin+color`
12, `skin+pelvis` 40, `skin+torso` 40, `naked` 80, `hair+color` 5,
`scalp+upper` 14, `scalp+lower` 0, `tattoo` 72, and the 94/66=15.9% baseline)
before any C++ was written, then cross-checked again after against the real
compiled binary's own `--explain-textures` alternate-counts (26/421/10/2 for
the four taggable HD slots) — exact match, confirming the shipped C++ behaves
identically to the validated design.

**Recall** (HD model, `(tag OR startswith) AND _hd`): pool 477 (was 94),
recall against the real 415-file ground truth: 379/415 = 91.3% (was
66/415 = 15.9%). Lower than this doc's own "any tag" figure (98.6%/1016
pool) because of the `_hd` partition — expected and correct, not a shortfall:
389 of 415 ground-truth files are themselves `_hd`, so the partition trades
away the 26 non-`_hd` outliers (plus a handful of naming edge cases) for
eliminating the non-HD model's poisoned pool. No equivalent ground-truth
recall figure exists for the non-HD model — confirmed via its own real
`--explain-textures` output (`--chr-model-id auto: ... no ChrModel row (not
a real player character model) -- skipping`), `bloodelffemale.m2` has no
derivable `ChrModelID` and therefore no DB2 customization-menu ground truth
to measure against at all; its own fix is verified by ledger delta instead
(below).

**Real ledger deltas**, `husk export --explain-textures`, before (commit
`a2ad9cb`, `Catalog::resolveFuzzyTier`) vs. after, four models:

- `creature/gnoll2/gnoll2.m2`, `item/objectcomponents/weapon/
  12be_bloodelf_crafting_brush01.m2`: **byte-for-byte identical.** Neither
  model's fuzzy-tier slots use a taggable type (gnoll2's own monster_1/
  monster_2 slots stay on the unchanged `filterCandidatesForType` path with
  an unchanged admitted pool — none of its 13 real candidates carry any of
  the 13 baked tokens; the item resolves entirely via tier 1/2, never
  reaching tier 3 at all). Confirms the common non-character case is
  untouched.
- `bloodelffemale_hd.m2`: slot 1 (type 6, hair) 12 → 26 alternates, same
  default (`hair_color_5196729.blp`). Slot 2 (type 1, skin) 64 → 421
  alternates, same default (`skin_color_3500122.blp`) — confirms the ranking
  heuristic stays correct even over a 6.6x larger candidate set. Slot 9
  (type 19, eyes) 9 → 10, same default. Slot 3 (type 20, jewelry) unchanged
  at 2/2, same default — the `jewelry`+`color` clause reproduces the old
  answer exactly. Slot 4 (type 2, object_skin, **not** a taggable type) 3 →
  386 alternates (the scan-level widening applies regardless of per-type
  tagging) and its *default changed*: from `bloodelffemale_hd_3255415.blp`
  to `bloodelffemaleskin00_00_hd.blp` — a real, incidental fix, not a
  targeted one: FileDataID 3255415 is the exact "tiny mostly-transparent
  sparkle/glint icon" this file's own `candidateAllowedForType` doc comment
  already names as a known-wrong historical default; the new, much larger
  admitted pool lets `orderCandidatesForDefault`'s pixel-area ranking find a
  real skin-shaped file instead, purely as a side effect of the wider scan
  gate.
- `bloodelffemale.m2` (non-HD — the real headline case): before, three
  distinct hardcoded slots (skin/type 1, object_skin/type 2, char_hair/type
  6) **all** resolved to the identical wrong file, `bloodelffemale_dh_horns.
  blp` (a demon-hunter horns texture), each reporting 891 "alternates."
  After: slot 0 (skin) 497 alternates, new default `bloodelf_female_dh_
  tattoo_00.blp` — not a perfect answer (confirmed directly: this non-HD
  folder has zero real `skin`+`color` files at all, so a DH tattoo variant
  really is the best real signal available locally), but no longer a horns
  texture, and no longer poisoned by the ~400 `_hd` files the old rule let
  leak in (a real prefix collision: `"bloodelffemale"` is a string-prefix of
  every `"bloodelffemale_hd_*"` filename). Slot 6 (char_hair) is the clean
  fix: no longer ambiguous at all — the `_hd`-partitioned, non-taggable-
  excluded pool has **exactly one** real hair-tagged candidate,
  `femhairbits.blp`, claimed deterministically. Slot 1 (object_skin) is
  **unchanged**, still defaulting to the same wrong `dh_horns.blp` — object_
  skin has no tag mapping (per this doc's own "unreachable" finding above),
  so this specific wrong default is a known, accepted, unfixed limitation,
  not an oversight.

**Verified by running the real compiled binary** against real local data for
all of the above (not reasoned about) — `husk export --explain-textures`
against the real `/media/luna/data/wow_export` corpus, before/after ledgers
diffed line by line, every delta traced to a specific code path. Not
verified: an actual Blender-side visual check of any of these new defaults
(same "Luna's own eyes" gate as every other texture-resolution change here).

### Supervisor re-verification (2026-08-29): two corrections

Both deltas above were reproduced independently from a separate pre-change
binary (commit `a2ad9cb`) and matched exactly. Two claims did not survive.

**1. "No real skin file exists locally for the non-HD variant" is false.**
48 real non-HD skin textures are on disk
(`bloodelffemalenakedpelvisskin00_*.blp`,
`bloodelffemalenakedtorsoskin00_*.blp`), and **26 of them are in the
non-HD skin slot's own candidate set after this change** — confirmed in
the emitted alternates. `orderCandidatesForDefault` ranked
`bloodelf_female_dh_tattoo_00.blp` above every one of them.

So the accurate statement is: **step 3 fixed the candidate set; the
ranking is now the binding constraint.** Recall genuinely improved and the
right answer is now reachable for the first time — it just isn't the one
picked. That is a strictly better failure than before (the right answer
was not even a candidate), and it makes ranking worth tuning for the first
time, which it never was at 15.9% recall.

**2. The HD `object_skin` change is a side effect, not a fix.** Type 2 has
no tag mapping and falls back to the unchanged category filter — but it
still inherits the *widened pool scan gate*, so its candidate count went
3 → 386 and its default changed. Nothing evaluated whether the new pick is
better; it is an arbitrary choice from a much larger unfiltered set. The
old default was known-wrong, so this is not obviously a regression, but it
is not evidence of improvement either. **Every untagged texture type now
picks arbitrarily from a much wider pool than before** — that is the main
risk this change introduces and it is not covered by any test.

**3. Output size moves hard in both directions, because every ambiguous
candidate is embedded as an `alternate_textures` extra.** Measured on the
same two models — material count and embedded-render-texture count are
identical before and after in both cases, so this is entirely diagnostic
payload, not render content:

| model | before | after |
|---|---|---|
| `bloodelffemale_hd` | 109 MB | **201 MB** (+84%) |
| `bloodelffemale` | 128 MB | **24 MB** (−81%) |

The non-HD drop is the `_hd` partition working exactly as intended — ~463
inapplicable candidates are no longer embedded. The HD growth is the
widened gate, and 201 MB for one character is a real usability problem for
Blender import. Narrowing the candidate set (steps 2 and 3 below) shrinks
this automatically; capping or dropping `alternate_textures` for very
large pools is the separate lever if it does not.

## Steps

1. **Make `Catalog::texture()` try a new DB2-character tier between tier 2
   (listfile) and tier 3 (fuzzy pool), scoped to exactly the case it's proven
   correct for**: given the model's real `CharComponentTextureLayoutsID`
   (already resolved upstream, same as `--char-layout-id`/`--chr-model-id`
   today) and the slot's own `textureType`, look up the live
   `ChrModelTextureLayer` rows for that (layout, type) pair from already-loaded
   `chrmodel::Data`. Fire *only* when that count is exactly 1 (a runtime
   check, never a hardcoded type list — see the Q1/Q4 findings above for why
   type 1/skin fails this on ~85% of real layouts, and why 6/9/20 pass it on
   this model but aren't guaranteed to elsewhere), *and* a
   `ChrCustomizationChoiceID` is resolved for whichever option feeds that
   target (explicit `--customization-choice-ids`, or the existing
   `defaultChoiceIdsForModel` heuristic), *and* `resolveChoice` for that
   choice actually yields a material for that target (it may not — see the
   Hair Color/Blindfold default-choice gaps in the Q3 table). Any of those
   three conditions failing is a normal, expected miss for this tier, not an
   error — fall through to tier 3 exactly as tier 1/2 already do. Needs new
   plumbing on `Catalog` (an optional injected `chrcustomization::Data` +
   `chrmodel::Data` + resolved choice map — `Catalog::texture()`'s current
   signature has no DB2 handle at all), not a one-line change inside the
   existing tiers.
2. **Rank the candidate set — it is now the binding constraint.** The set
   fix landed; the picker did not. Measured: the non-HD skin slot has 26 real
   `nakedtorsoskin`/`nakedpelvisskin` files among its candidates and
   `orderCandidatesForDefault` still ranks `bloodelf_female_dh_tattoo_00.blp`
   above all of them. Ranking by decoded pixel area plus filename category was
   built for a 94-candidate starved pool and is now choosing among hundreds.
   This is the highest-value remaining item: the right answer is reachable for
   the first time and is not being picked.
3. **Constrain the widened pool for untagged texture types.** Types with no
   tag clause (`object_skin` and every non-character replaceable type) fall
   back to the old category filter but still inherit the *widened scan gate*,
   so they now pick arbitrarily from a much larger set — HD `object_skin` went
   from 3 candidates to 386. No test covers this and no evidence says the new
   picks are better. Either give those types a real clause, or keep the narrow
   gate for types that have none.
4. **Decide claim-and-remove on measured behavior, not its stated purpose.**
   It does **not** currently provide the "one image can't fill every slot"
   property it is described as providing: `Catalog::resolveFuzzyTier` only
   erases from the pool in the `matching.size() == 1` branch, so the ambiguous
   branch — the case that dominates character models — never depletes anything.
   That is why three distinct slots on the non-HD model all resolved to the
   same file before this change. Removing it is therefore close to a no-op for
   the ambiguous path; the real question is whether the sole-candidate branch's
   depletion is worth keeping on its own.
5. **Re-run the resolution-ledger diff** (`husk export --explain-textures` /
   `husk resolve`, `REFACTOR/README.md`'s stage-1 gate) after each of the
   above, the same way the "Step 3/5 findings" section already did. Every
   delta attributed; deltas here are expected and are the point.

## Gate

Independent to implement and measure. The final "does this character look right
in Blender" call is Luna's, as always.

## Reproducing the numbers

Ground truth = DB2-named FileDataIDs for the model, resolved through
`--listfile` to real paths, intersected with what is on disk. Note the
community listfile is **CRLF** — an unstripped `\r` silently makes every path
miss, which cost this investigation one wrong "0 files exist" conclusion before
it was caught.
