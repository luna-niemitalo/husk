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
worse than a one-time careful rename. Item 1 (cameras) is low-priority by
design, not by oversight; items 2/3 are awareness-only footnotes, not
action items, kept here only so they aren't lost; item 4's LOD hypothesis
has since been ruled out by real data, and the extras-export half is now
implemented (`husk export --bones-dir`) — what's left needs a client-side
DB2 lookup husk doesn't have access to, not more file-reading or export
work. Formerly-tracked items that got fixed and folded back into
`README.md`/`DESIGN.md` (shell completion, `.phys`/`PFID` surfacing,
`M2Ribbon`, `M2Particle`, and everything `FINDINGS.md` used to track before
it was retired) were removed from this file entirely.

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

### 3. Multi-texture-layer index arithmetic unverified against a real file

`cmd_export.cpp`'s `textureComboIndex + layer` / `textureCoordComboIndex +
layer` arithmetic for a batch's 2nd+ texture layer (see the code comment
right above that arithmetic) is implemented straight from wiki prose, with
an in-code note that it hasn't been cross-checked against a real
multi-layer file the way nearly everything else in this codebase has been
(this project's own stated bar, per `DESIGN.md`'s recurring "decode real
records... don't guess from text alone" principle). Worth a real-file check
whenever a multi-texture-layer test model shows up in `test_data/` —
`WIKI_FINDINGS.md`'s methodology is the template.

---

### 4. `.bone` correction matrices — which slot applies is unresolvable, extras export is done

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
