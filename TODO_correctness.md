# TODO: correctness &amp; usability gaps

**Status: an open punch list, not a historical record.** Fixed items get
removed outright rather than kept as `[Fixed]` noise — git history is where
the record of what was fixed and when lives, not a checked-in file.

**Note from an earlier conversation, relevant to item 3 below (`.bone`
slot selection) now that real local DB2 access is confirmed in scope
(`CHAR_TEXTURE_COMPOSITING_TODO.md`'s own Background) — scope clarified
directly by Luna (2026-08-08, two passes)**: a DB2→SQLite exporter is a
separate side project, not part of the real pipeline — "the real pipeline
is the same as with modern blp's — read the file, transform in memory,
write to gltf." Whatever resolves this item's `.bone`-slot-selection
lookup (a `ChrCustomizationBoneSet`-shaped table, if it exists locally)
reads the real WDC5 `.db2` bytes directly at runtime, no SQLite
round-trip. The SQLite side project itself is bigger than a debugging
convenience, though: a real relational schema (mapping/join tables for
DB2 tables' real foreign-key relationships, not one flat table per file),
serving two purposes — a correctness cross-check for the WDC5 parser, and
a general-purpose local data source for other consumers of this project's
work beyond `husk export` itself, expected to matter a lot once WMO/ADT
world-data implementation starts. Full detail, don't duplicate further:
`CHAR_TEXTURE_COMPOSITING_TODO.md`'s own note.

Former item 1 (particles, `M2Particle`) is exactly that case — a real
weapon-model corpus (Luna's own extraction, `test_data/item/
objectcomponents/weapon/`) finally made the "needs real-file investigation"
blocker addressable: `M2Particle` is now fully parsed (every static field,
every animation curve — FBlock-based color/alpha/scale/UV curves and
`M2Track<float>` simulation parameters alike) for version ≥
`kMinVerifiedParticleVersion` (272, Cataclysm — the shape genuinely changed
there; older versions are real but unverified, not attempted), split
between a minimal `.glb` extras placement anchor and `husk dump-chunks`'s
full JSON output (see `DESIGN.md`'s Key design decisions, `WIKI_FINDINGS.md`
for the real-data cross-check). `M2Ribbon`'s own remaining tracks got
finished in the same pass. Removed outright rather than kept as a done-item
note, and every remaining item below renumbered accordingly (1-4, was
2-5) — a deliberate, fully cross-checked exception to "don't renumber, it
touches live code strings," since leaving a numbering gap forever would be
worse than a one-time careful rename.

Former item 3 (multi-texture-layer arithmetic) is now resolved the same
way: a full real-data scan (Luna's own extraction, ~287k `.skin` files and
~130k `.m2` files) found and confirmed both a real `textureCount > 1` batch
and a real nonzero `textureCoordCombos` table, hand-verified byte-for-byte
against an independent parse (see `WIKI_FINDINGS/M2/skin.md`) and now backed by
permanent real-data regression tests (`tests/test_integration.cpp`'s
`checkMultiTextureLayerArithmetic`, gated on `test_data/world/
replaceabletextureprops/guild/pennant_guild_alliance_a_01.m2` and
`test_data/world/expansion05/doodads/ironhorde/
6ih_ironhorde_siegeweapon03.m2`). Removed outright and the remaining item
renumbered accordingly (1-3, was 1-4) — same one-time exception as above.

Item 1 (cameras) is low-priority by design, not by oversight; item 2 is an
awareness-only footnote, not an action item, kept here only so it isn't
lost; item 3's LOD hypothesis has since been ruled out by real data, and
the extras-export half is now implemented (`husk export --bones-dir`) —
what's left needs a client-side DB2 lookup husk doesn't have access to,
not more file-reading or export work. Former item 4 (`M2Sequence.aliasNext`
resolution) is resolved outright, not just further investigated: a
`M2_UNKNOWNS_EXPLORATION.md` investigation pass found the field is a plain
local index into the same file's own `sequences` array (at the real,
`M2Bounds`-corrected byte offset 0x3E, not the wiki's literal
pre-correction 0x22) — see `WIKI_FINDINGS/M2.md` for the full evidence,
including what the earlier "doesn't resolve" finding actually got wrong
(reading the field at the wrong offset). Removed outright per this file's
own convention rather than left as a resolved-but-lingering item; the
follow-up (using this to produce real animation clips for currently-
skipped alias sequences) was itself implemented and removed from
`M2_GAPS_TODO.md` (its former Item 1) — see `DESIGN.md`'s Key design
decisions. Formerly-tracked items that got fixed and folded back into
`README.md`/`DESIGN.md` (shell completion, `.phys`/`PFID` surfacing,
`M2Ribbon`, `M2Particle`, multi-texture-layer arithmetic, and everything
`FINDINGS.md` used to track before it was retired) were removed from this
file entirely.

---

## Read-pipeline correctness

### 1. Cameras (`M2Camera`) — low priority, explicitly deprioritized

**Explored per request: why would a custom-engine emulator need these at
all?** `M2Camera` records are WoW's own *baked, model-relative* cinematic
camera paths — used for things like the character-select rotating camera,
some cutscenes, and login-screen framing. They are fixed viewpoints
authored by Blizzard for *their* UI/cinematic contexts, not something a
model needs in order to render correctly from an arbitrary camera a custom
engine already owns. Unlike billboarding, nothing about normal model
rendering depends on `M2Camera` data — a custom renderer supplies its own
camera unconditionally.

**Verdict: low priority.** Worth only if the goal ever expands to
literally reproducing WoW's specific character-select/cinematic screens,
not general model rendering. Currently count/offset-only in `husk info`;
leave as-is.

---

### 2. Five lookup-table arrays parsed, never referenced

`boneLookup`, `attachmentLookup`, `cameraLookup`, `textureLookup`,
`sequenceLookup` (`src/m2.hpp`) are all read into descriptors and never
dereferenced or counted anywhere downstream. Lowest priority here — these
are indirection/name-lookup tables (key-bone role lookup, replaceable-
texture lookup, name-lookup for cameras/attachments), not required for the
mesh/skin/material/animation pipeline that's already implemented, and
husk's own documented design choice (full per-vertex global joint indices
instead of hardware bone-limit batching) already makes the closely-related
`boneCombos` moot by intent, not oversight. Awareness-only, in case any of
them become relevant to future work (e.g. `attachmentLookup` would matter
if attachment-point *naming* — not just raw id/bone/position, which `husk
info` already prints — ever gets added).

---

### 3. `.bone` correction matrices — which slot applies is unresolvable, extras export is done

`husk dump-chunks <file.bone>` surfaces the raw `(bone_index, matrix)`
pairs (see `README.md`'s `.bone` section); nothing about which of a
model's several `.bone` files (per its `BFID` array) applies to which
context is documented anywhere, on the wiki or otherwise.

The LOD/render-distance hypothesis is ruled out by real data
(`WIKI_FINDINGS/BONE.md`'s follow-up). All 20 `.bone` files
`bloodelffemale_hd.skel`'s `BFID` lists don't fit `LDV1`'s `lod_count: 7`
at all (20 vs. 7, no clean relationship), collapse into only 5 distinct
bone-index sets with heavy exact-duplication (one 33-bone set repeats
verbatim across 10 of the 20 files), and where a bone is corrected across
multiple files the correction is a pure magnitude scale along one of just
two fixed directions — the signature of a small number of shape variants
reused across many selectable slots (several sharing texture/color-only
choices), not a per-LOD detail-reduction ladder. The corrected bones
themselves cluster tightly around the model's Head/Jaw bones (parent chain
confirmed via `.skel`'s own `SKB1` records), nowhere near hand/grip or
armor-fitting bones — ruling out a weapon-type or armor-type selector too.

**Extras export: done.** `husk export --bones-dir <dir>` resolves every
FileDataID the model's/`.skel`'s `BFID` array declares to a real
`<dir>/<FileDataID>.bone` file (same local-directory, never-CASC
convention as `--textures`/`--skin-dir`/`--anim`), parses it via the
existing `husk::bone::parse`, and attaches every resolved slot's
`(bone_index, matrix)` pairs as `bone_correction_sets` on the exported
glTF skin's `extras` — inert, never applied to the bind pose or any
animation (see `DESIGN.md`'s "Key design decisions" for why, `README.md`'s
Usage section for the flag itself).

**What's still open, and why it can't be closed here:** which in-game
customization choice picks `BFID[7]` vs. `BFID[13]` most plausibly lives in
client-side DB2 data (a `ChrCustomizationBoneSet`-shaped table, going from
memory — not confirmed against a real DB2 dump) that husk has no access to
and, per `DESIGN.md`'s non-goals, never will at runtime. That's a genuinely
different, external lookup — not something more file-reading or export
work inside husk can resolve. If that mapping ever becomes available some
other way (e.g. a separate out-of-band tool scraping it from CASC/DB2 at
build time, per `DESIGN.md`'s Non-goals, and handing husk a plain slot
index/file to use), applying a specific slot to the render becomes a real
follow-up; not attempted here.

---

### 4. Texture-transform pivot-correction math (constant case) — real fixture found, math not yet implemented

`M2_COMPLETENESS.md`'s "Texture transform (constant case)" row is capped
at `native-possible, unverified`, not `extras-capped, permanent` like the
animated case right below it — `gltf::Material::textureTransform` is
currently written as inert `extras` only (`src/m2.hpp`'s `TextureTransform`
doc comment, `DESIGN.md` lines ~308-328), but a real `KHR_texture_transform`
*is* representable for the constant case, unlike the animated one (core
glTF's `KHR_texture_transform` has no animation-channel target at all, so
that half stays permanently extras-only regardless of effort). What's
blocked implementation so far: per wowdev.wiki, M2's rotation pivots around
the texture's own center (0.5, 0.5), while `KHR_texture_transform`'s
rotation pivots around (0,0) — folding that pivot difference correctly
into the extension's `offset` field is exactly the kind of math this
project's own methodology says shouldn't ship without a real file to check
it against (`WIKI_FINDINGS.md`: decode real records, don't guess from text
alone), and no such fixture existed.

**A real fixture now exists.** `tools/find_texture_transform_files.py`
(new, same self-contained one-off-scanner convention as
`find_multiroot_skeletons.py`) scanned the full real corpus
(`/media/luna/data/wow_export`, 130,576 files, ~35s): 517 files carry any
`M2TextureTransform` data at all; 761 individual transform records are
unambiguously constant (`constantTrackValueOffset`'s exactly-one-keyframe
rule, `src/m2.cpp:639`) with a non-identity rotation; 401 of those are
confirmed actually referenced by a real `.skin` batch's
`textureTransformComboIndex` (not dead/unused array entries) — full path
list in the gitignored `texture_transform_files_for_exploration.txt`
(repo root), same convention as every other `*_for_exploration.txt`. The
rotations found are clean, discrete angles (mostly exact 90°/180°/67.5°
about Z), consistent with authored UV-orientation flips rather than
continuous scroll effects — real, sane data, spot-checked against an
independent hand-decode of one file (`brewfestmount.m2`, matched exactly,
magnitude-1.0 quaternion) before being trusted.

**Best candidate fixtures**, in order of simplicity:
- `creature/brewfestmount/brewfestmount.m2`, transform index 0 — rotation
  `(0,0,-1,0)` (180° about Z) with no translation/scaling on that record
  at all. Simplest possible case, isolates rotation-pivot math from
  everything else.
- `creature/boundairelemental/unboundairelemental_low.m2`, transform
  index 0 — rotation `(0,0,0.7071,0.7071)` (90° about Z), same shape.
- `creature/bloodknightcharger/bloodknightcharger.m2`, transform index 2
  — rotation `(0,0,-1,0)` *plus* constant scaling `(1.0, 1.5, 0.0)`, a
  good second fixture once rotation-alone is verified, since it exercises
  rotation and scale combined.

**Not yet done, and what it would take:** (1) derive and hand-verify the
pivot-correction formula (texture-center-pivot rotation → equivalent
`KHR_texture_transform` offset+rotation pair) against `brewfestmount.m2`'s
known values, the same way every other wiki-sourced formula in this
project gets cross-checked before shipping; (2) copy the chosen fixture(s)
into `test_data/` (none of the three above are committed yet — same
"real test data was the actual blocker" pattern the particle/ribbon and
`.phys` sessions hit); (3) wire the corrected math into
`cmd_export.cpp`'s existing texture-transform-extras block
(`src/cmd_export.cpp:1178`) as a real `KHR_texture_transform` on the
material when the transform is unambiguously constant, falling back to
today's inert-extras behavior when it's animated; (4) verify via
`gltf_validator` and a Blender readback (UV coordinates match the source
data's own intent), same conformance discipline `tests/
test_conformance.cpp` already applies elsewhere. Investigation only this
round — no `src/` changes, no fixture committed yet.
