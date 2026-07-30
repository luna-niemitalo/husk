# TODO: correctness &amp; usability gaps

**Status: an open punch list, not a historical record.** Fixed items get
removed outright rather than kept as `[Fixed]` noise — git history is where
the record of what was fixed and when lives, not a checked-in file.
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
against an independent parse (see `WIKI_FINDINGS.md` §7) and now backed by
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
not more file-reading or export work. Formerly-tracked items that got
fixed and folded back into `README.md`/`DESIGN.md` (shell completion,
`.phys`/`PFID` surfacing, `M2Ribbon`, `M2Particle`, multi-texture-layer
arithmetic, and everything `FINDINGS.md` used to track before it was
retired) were removed from this file entirely.

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
(`WIKI_FINDINGS.md` §4's follow-up). All 20 `.bone` files
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

### 4. Alias sequences (`M2Sequence.flags & 0x40`) — "unresolvable" needs re-checking, not assuming

`buildAnimations` (`src/cmd_export.cpp`) treats any sequence with `flags &
0x40` set (and `0x20` unset) as a dead end and skips it outright, citing
wowdev.wiki directly in the code comment: `// wowdev.wiki: "I have no clue"
where this lives.` That quote is real, but it's from an older bullet-point
summary partway down the M2 wiki page (`M2#Animation_sequences`) — the same
page's own struct listing and Flags table, a little further down, are more
specific and arguably contradict it: `M2Sequence` has a real
`uint16_t aliasNext` field at offset `0x22` ("id in the list of animations.
Used to find actual animation if this sequence is an alias"), and the Flags
table spells out an actual mechanism: "the client skips these by following
`aliasNext` until an animation without `0x40` is found." husk's own
`m2::Sequence` doesn't even parse this field currently — `m2.hpp`'s doc
comment lists `aliasNext` under "deliberately skipped, extend as later
commands need more."

**Not yet resolved even at a should-we-bother level — this is an
exploration task, not a confirmed dead end.** A first real-data check
against `bloodelffemale_hd.skel` (7 alias sequences out of 396) found
`aliasNext` values in the 48861–48983 range, which don't resolve cleanly
either way tried: too large to be a local index into this file's own
396-entry `sequences` array, and no match against any other sequence's own
`id` field within the same file. That leaves at least two live
possibilities, neither confirmed:

- `aliasNext` indexes into a global, client-side `AnimationData.dbc`-scale
  table husk has no access to — the same class of external-lookup blocker
  item 3 above (`.bone` slot selection) already hit, in which case this
  really is unresolvable from the file alone and the wiki's "I have no
  clue" *is* correct in effect even though a mechanism is named.
- It resolves against something reachable from the file(s) husk already
  has, just not the naive things a first pass tried — e.g. matching against
  a wider id-space than one `.skel`'s own 396 sequences (a model's
  companion `.m2`/other `.skel`s?), a relationship involving the
  neighboring `variationNext` field, or a per-sequence-block indexing
  scheme this quick check didn't consider.

Worth a real investigation pass before concluding either way: check whether
anyone on wowdev.wiki's talk/revision history has resolved this since that
older bullet point was written; check a wider real-file sample than one
model's 7 aliases (do `aliasNext` values ever fall in-range for *some*
file's own sequence count?); check whether other established M2 tooling
(`wow.export`, `pywowlib`, etc.) resolves alias sequences at all, and how.
Currently 7/396 sequences (~1.8%) on the project's own primary fixture —
low blast radius either way, but worth knowing which bucket this is
actually in rather than leaving `buildAnimations`'s skip un-investigated
on the strength of one possibly-outdated wiki sentence.
