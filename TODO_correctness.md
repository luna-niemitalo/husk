# TODO: correctness &amp; usability gaps

A forward-looking companion to `FAILURES.md` (already-fixed bugs) and the
README's format-support matrix (feature coverage). This file tracks specific
gaps found while assessing husk's M2 read pipeline against the goal of a
**1:1 emulation of a WoW M2 model in a custom renderer** — not "opens
correctly in Blender," a higher bar.

**Status: items 1, 2, 5, 6, and the CLI defaults section are fixed** (each
with regression tests — `tests/test_m2.cpp`, `test_gltf.cpp`, `test_cli.cpp`).
Items 3, 4, and 7 remain open — 3 and 4 need real-file reverse-engineering
work this pass deliberately didn't attempt (guessing at an undocumented
binary format risks silently-wrong data, worse than no data); 7 is
low-priority by design, not by oversight. Left in place as a record of what
was found and fixed, not as a stale list.

---

## Read-pipeline correctness

### 1. [Fixed] `M2Track.interpolation_type` / `.global_sequence` were never read

**What was wrong:** `trackSequenceInnerArrays` (`src/m2.cpp`) read the outer
timestamps/values arrays without ever looking at the two fields before them
— `interpolation_type` (`+0x00`) and `global_sequence` (`+0x02`). Two real
consequences: a `global_sequence`-driven track (continuous, sequence-
independent looping — glow pulses, cloth sway) would get silently
attributed to whichever `M2Sequence` happened to occupy the track's
outer-array position 0, rather than being recognized as unresolvable; and
`gltf.cpp` hardcoded every animation sampler's interpolation to `LINEAR`
regardless of the source type.

**Fixed:** `m2::readTrackMeta` reads both fields; `resolveVec3TrackSequence`/
`resolveQuatTrackSequence` now return empty (not misattributed data) for a
real `global_sequence` track, and throw `ParseError` if `interpolation_type`
is 2/3 (cubic bezier/hermite) — confirmed via the local wowdev.wiki mirror
that bone/color/weight tracks are plain `M2Track<T>`, never
`M2SplineKey<T>`, so 2/3 there would mean a version this parser doesn't
understand, not a case to guess a 3x stride for. `gltf::JointAnimation` got
per-property `translationStep`/`rotationStep`/`scaleStep` flags, wired from
`interpolation_type == 0`, so glTF samplers now emit `STEP` correctly
instead of always `LINEAR`. Verified against real data: `bloodelffemale.m2`
(256 clips) and the `.skel`-sourced `bloodelffemale_hd.m2` (334 clips) both
produce byte-identical clip counts before and after — the fix changes
nothing for tracks that were already correct, only for the
previously-mishandled cases.

---

### 2. [Fixed] Material render flags: only two-sided was translated

**What was wrong:** `cmd_export.cpp` only read `M2Material.flags & 0x04`
(two-sided); `0x01` (Unlit — confirmed via the wowdev.wiki mirror's own
flags table) was dropped, so an unlit-flagged material (glow/eye-effect
layers) rendered as normally-lit.

**Fixed:** `gltf::Material::unlit`, set from the material's `0x01` bit,
translated to glTF's `KHR_materials_unlit` extension (with a matching
`extensionsUsed` entry). depthTest/depthWrite (`0x08`/`0x10`) still have no
core-glTF equivalent, so they're deliberately not translated.

---

### 3. `.skel`-sourced external `.anim` files (`AFSB`) are unparsed

**Still open — deliberately not attempted this pass.** All 54/54 sampled
`.anim` files for the `.skel`-sourced HD test model carry `AFSB` instead of
usable `AFM2` data. For any Legion+ character whose bones live in a `.skel`
file (the norm for modern character models), external-sequence animation is
essentially 0% available. `AFSB`'s own byte layout is undocumented anywhere
(see `WIKI_FINDINGS.md` §2) — a genuine reverse-engineering task, not a
quick parser fix.

---

### 4. Particles (`M2Particle`) / ribbons (`M2Ribbon`)

**Ribbons: fixed** (see below). **Particles: still open.**
`M2Particle` is a large, heavily version-conditional struct (BC/Cata+
branches for several fields) — enough real-file investigation to get right
is out of scope for this pass. Currently count/offset-only in `husk info`.
Still core to WoW's visual identity (weapon trails, magic, fire/smoke) and
0% implemented.

**`M2Ribbon`: fixed.** `m2::Ribbon`/`m2::parseRibbons` (`src/m2.cpp`,
0xB0-byte record confirmed against the wowdev.wiki mirror) surfaces the
static fields — `ribbonId`/`boneIndex`/`position`/`edgesPerSecond`/
`edgeLifetime`/`gravity`/`textureRows`/`textureCols` — same
static-fields-only pattern as Attachment/Event/Light; the six embedded
`M2Track`s (color/alpha/height/texSlot/visibility) and the two
`M2Array<uint16_t>` lookup tables are left unread, consistent with that
pattern. Surfaced via `husk info`.

---

### 5. [Fixed] Billboarding

**What was wrong:** `M2CompBone.flags`' billboard-mode bits (spherical,
cylindrical-lock-X/Y/Z — confirmed exact hex values via the wowdev.wiki
mirror) were already captured in `Bone::flags` but never named or exposed
anywhere.

**Fixed:** `m2::BoneFlag` namespace names the four bits;
`m2::billboardModeName` maps a bone's flags to a mode string (or `nullptr`
for "not billboarded" — the ordinary case). `husk info` prints a line per
billboarded bone. `gltf::Skeleton::Joint::billboardMode` carries the same
string through `husk export`, landing in the joint's glTF node `extras` as
a `"billboard"` key — metadata for a custom renderer's own camera to act
on, not something writeGlb applies itself (billboarding is inherently
renderer-camera-relative, unrelated to `M2Camera`, see item 7). Verified
against real data: `bloodelffemale.m2` has 10 real spherical-billboard
bones, all correctly identified.

---

### 6. [Fixed] `.phys` / `PFID`

**Fixed:** `Header::physFileId`, wired through `resolveBlob` exactly like
`SKID`/`skeletonFileId` (confirmed `PFID` is a single `uint32_t`, same
shape as `SKID`, via the wowdev.wiki mirror — not an array like
`BFID`/`AFID`). Surfaced via `husk info`. Actual `.phys` file *content*
parsing is still unstarted — this only closes the "not even surfaced"
gap, matching where `SKID` was before `.skel` support existed.

---

### 7. Cameras (`M2Camera`) — low priority, explicitly deprioritized

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

## CLI usability — [Fixed] no reasonable defaults anywhere

**All four items below are fixed**, each with regression tests in
`tests/test_cli.cpp` covering: the fully-defaulted single-argument
invocation end to end, the `_hd`-variant false-match trap specifically, the
multiple-candidate/ambiguous case, the no-match error case, `.skel`
auto-detection, and `--textures`/`--anim-dir` defaulting to the model's own
directory. Verified against real data: `husk export bloodelffemale.m2`
(model path alone, from `test_data/`) and `husk export bloodelffemale_hd.m2`
(alone, from its own directory) both now work end to end and produce
byte-identical vertex/triangle/bone/animation counts to the fully-explicit
invocations documented in the README.

- [x] **`.skin` path is mandatory** → now optional. An omitted `.skin` path
      scans the model's own directory for `<model-basename><N>.skin`
      (`cmd_export.cpp`'s `findSameBasenameSkins`) — deliberately requiring
      the character right after the basename to be a digit, specifically to
      avoid matching a real, unrelated, higher-poly model whose name merely
      *extends* this one's as a string (`bloodelffemale_hd00.skin` for
      `bloodelffemale.m2`). Multiple matches resolve to the lowest-numbered
      (highest-detail) one, noted on stderr; zero matches is a clear error.
      The literal `auto` keyword's FileDataID-based mechanism is untouched
      and still available as an explicit alternative.
- [x] **`.skel` path required positional** → now optional. When a model's
      inline bones are empty and no `.skel` path was given, `husk export`
      checks for `<model-basename>.skel` next to the model (exactly the
      README's own `bloodelffemale_hd.m2` + `bloodelffemale_hd.skel`
      shape) before falling back to the unskinned-mesh output it always had.
- [x] **`--textures`/`--skin-dir`/`--anim-dir` required every time** → all
      three now default to the model's own directory when not given
      explicitly, with the flags still available as overrides for the
      FileDataID-renamed-directory workflow.
- [x] **Output `.glb` path mandatory** → now optional, defaulting to
      `<model-basename>.glb`.

All three positionals (`.skin`, output, `.skel`) are **trailing-optional**
(you can stop early, same as `cp src [dst]`) — every existing
2/3/4-positional invocation is completely unaffected; this only adds
shorter ones. Every default still fails loudly on ambiguity or a missing
file rather than silently guessing, consistent with the project's existing
foreign-data-validation convention (see `FAILURES.md`).
