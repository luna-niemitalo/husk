# TEXTURE_POOL_RECALL_TODO.md — fuzzy texture pool recall/ranking, and adjacent resolution-code bugs

Open punch list. Fixed items get removed outright; git history is the record.

**Scope: dual-use/infrastructure.** The fuzzy-candidate-pool/ranking
problem is a semantic disambiguation question, not a legacy-writer quirk —
`canon::`'s own texture resolution will hit the same "which candidate is
actually correct" question and can reuse the ranking/tag work here.

Full narrative for everything below (the original 84%-excluded-candidates
finding, the tag-vocabulary derivation, the DB2-character tier, the `_hd`
partition, every real ledger delta measured along the way): `CLAUDE_HISTORY.md`'s
2026-09-16 archival entry.

## Background

`scanFuzzyTexturePool` originally admitted candidates only via
`stem.startswith(model_basename)` — measured against real ground truth for
`bloodelffemale_hd`, that rule excluded 84% of the correct files (66/415
recall). Root cause: `character/bloodelf/female/` alone uses at least four
naming conventions, and the old rule matched only one. Fixed via a real,
corpus-derived tag vocabulary (13 tokens, `kTextureTagVocabulary`) admitting
candidates by tag conjunction instead of prefix, plus a hard `_hd` partition
(HD and non-HD files never cross-pollute), plus a new DB2-driven
`db2-character` tier that precedes the fuzzy pool entirely when a slot has
exactly one live `ChrModelTextureLayer` target and a resolved customization
choice with a real material row. Recall on `bloodelffemale_hd` went from
15.9% to 91.3%.

**Where this leaves things**: the candidate *set* fix landed and is verified
byte-for-byte against real exports (non-character models untouched, character
models measurably improved or unchanged). The right answer is now reachable
for the first time on the non-HD skin slot — but `orderCandidatesForDefault`
still doesn't pick it. **Ranking, not recall, is now the binding constraint.**
Candidate-set size is also the dominant runtime cost now (measured: 4.2x
slower on the HD model's now-421-candidate skin slot, since every candidate
gets BLP-decoded to rank by pixel area and then embedded as an
`alternate_textures` extra) — a ranking fix that avoids decoding every
candidate pays twice.

## Steps

1. **Rank the candidate set — it is now the binding constraint.** The set fix
   landed; the picker did not. Measured: the non-HD skin slot has 26 real
   `nakedtorsoskin`/`nakedpelvisskin` files among its candidates and
   `orderCandidatesForDefault` still ranks a demon-hunter tattoo texture above
   all of them. Ranking by decoded pixel area plus filename category was built
   for a 94-candidate starved pool and is now choosing among hundreds. Highest-
   value remaining item: the right answer is reachable and isn't being picked.
2. **Decide claim-and-remove on measured behavior, not its stated purpose.**
   It does **not** currently provide the "one image can't fill every slot"
   property it's described as providing: `Catalog::resolveFuzzyTier` only
   erases from the pool in the `matching.size() == 1` branch, so the ambiguous
   branch — the case that dominates character models — never depletes
   anything. Removing it is therefore close to a no-op for the ambiguous path;
   the real question is whether the sole-candidate branch's depletion is worth
   keeping on its own.
3. **Re-run the resolution-ledger diff** (`husk export --explain-textures` /
   `husk resolve`, `REFACTOR/README.md`'s stage-1 gate) after each of the
   above. Every delta gets attributed; deltas here are expected and are the
   point.

## Open item: fuzzy resolution can attach a wrong (not just missing) texture to a runtime-only slot

(Merged from the now-deleted `TODO/TODO_correctness.md` #4 — same resolution
code, an adjacent bug class to the ranking problem above, not folded into
"## Steps" since it's a distinct root cause.)

Found visually inspecting `item/objectcomponents/head/
helm_plate_raiddeathknightulatek_d_01_wo_f.m2` against a real in-game
reference: its `weapon_blade` (M2 texture type 3) decorative element should
read as a subtle green glow but exports as a solid brown/tan panel using the
helmet's own metal-shell texture.

Root cause: `candidateAllowedForType()` (`export_texture_resolution.cpp`)
only restricts a fuzzy candidate when its filename category is *recognized*
(`candidateCategoryTypes()`'s fixed character-customization vocabulary —
`skin_color`, `hair_color`, etc.). Quality-tint tokens like `blue`/`green`/
`orange` aren't in that table, so per the function's own documented
"unrecognized categories are always allowed" default, they get offered to
*every* ambiguous slot — including `weapon_blade`, which has nothing to do
with quality-tint recoloring. Confirmed directly: both this file's real
`object_skin` materials *and* every `weapon_blade` material resolved to the
same image.

**Not fixed.** Likely shape: `candidateAllowedForType`/`candidateCategoryTypes`
need a way to say "this texture type accepts only recognized categories,
never the unrecognized-category fallback" for types that are purely
runtime-populated with no static local equivalent (`weapon_blade` confirmed;
worth auditing the rest of the 1-23 range for the same shape before
generalizing). Corrected behavior would leave `weapon_blade` honestly
unresolved (`fileDataId: 0`, no `baseColorImagePng`) rather than confidently
wrong. Not corpus-quantified — worth a scan across every file with 2+
ambiguous slots sharing one unrecognized-category candidate pool before
treating this as high-priority.

## Open item: full-corpus render + visual spot-check (human-gated)

(Merged from the now-deleted `TODO/KNOWLEDGE_BASE_DESIGN.md` — its own
object-skin-resolution fix, a local race/gender-suffix-stripping fallback
tier in this same fuzzy matcher, landed 2026-08-16 and is not itself open;
this is its direct, still-outstanding follow-up.)

`render_sample_driver.py`/`tools/full_render.py` need a real run to
completion (`direnv exec . tools/venv/bin/python tools/full_render.py`),
then a real visual check of the output, before this can be called verified
at corpus scale. **Marked human-gated per Luna's explicit correction** — this
exact spot cost a previous session real self-inflicted damage twice (an
unauthorized `rm`, an inline/foreground full-corpus render blocking the
session). Get Luna's go-ahead before starting the render, run it backgrounded,
and hand the visual review to her rather than self-certifying it.

## Open item: cross-validate DB2 object-skin candidates against the fuzzy pool (not started)

(Merged from the now-deleted `TODO/KNOWLEDGE_BASE_DESIGN.md`'s "Proposed
robustness follow-up," 2026-09-04.)

`src/itemappearance_db2.hpp`/`.cpp` (built for `husk appearance-string`)
walks `ItemModifiedAppearance -> ItemAppearance -> ItemDisplayInfo`, so by
construction it only ever reaches an `ItemDisplayInfoID` a real
`ItemAppearance` row references — exactly the existence check the old
`model_object_skin_texture` DB2 join was missing (it trusted
`ItemDisplayInfoID`s that turned out to be orphaned/stale data pointing at
unrelated items' textures, confirmed via `helm_leather_pvpdruid_b_02_scm.m2`
resolving to an unrelated staff texture). It's just never been pointed back
at the object-skin problem.

Proposed shape:

1. For a model's reverse-derived `ItemDisplayInfoID` candidates, filter to
   only the ones reachable from some real row in `itemappearance_db2`'s
   `ItemAppearance` table (a plain existence check on `ItemDisplayInfoID` —
   no `ItemModifiedAppearanceID` needed).
2. Cross-check that filtered, DB2-validated set against the local fuzzy-match
   pool already computed for the same model:
   - **Both agree on one candidate** -> high-confidence single answer, usable
     as the default without the fuzzy path's "please confirm" warning.
   - **DB2-validated set empty, name-match isn't** -> fall back to today's
     fuzzy-match behavior unchanged (the common case today).
   - **Both non-empty but disagree, or either side is itself ambiguous** ->
     never silently pick one; surface every candidate from both sources as
     `alternate_textures` and flag the *disagreement itself*, a stronger
     signal than either source's own internal ambiguity.
   - **Both empty** -> genuinely unresolved, report as such.

Generalizes past this one join: anywhere husk has two independently-derived
candidates for the same fact (an authoritative-but-sometimes-corrupt DB2
chain, a locally-verifiable heuristic), prefer *agreement* as the confidence
signal and treat disagreement as stronger evidence of a problem than either
signal's own uncertainty. Not started; doesn't need further DB2
re-extraction — `ItemAppearance` alone is enough for the existence check —
but worth measuring how complete/current the local `ItemAppearance` table
actually is first.

## Gate

Steps 1-3 above are independent to implement and measure. The final "does
this character look right in Blender" call is Luna's, as always. The
full-corpus render item above is explicitly human-gated (see its own note).

## Reproducing the numbers

Ground truth = DB2-named FileDataIDs for the model, resolved through
`--listfile` to real paths, intersected with what is on disk. Note the
community listfile is **CRLF** — an unstripped `\r` silently makes every path
miss, which cost the original investigation one wrong "0 files exist"
conclusion before it was caught.
