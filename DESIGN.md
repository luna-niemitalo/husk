---
aliases:
  - README
---
# DESIGN.md — husk

Architecture and design rationale. Status/progress lives in the README's
roadmap and format matrix, not here — this file explains *why* the code is
shaped the way it is, so a structural change can be checked against the
reasoning instead of just the current state. Real-file reverse-engineering
findings live in `WIKI_FINDINGS.md`; open correctness gaps live in
`TODO/TODO_correctness.md`; a granular per-M2-feature completion breakdown
(parse depth vs. consumption depth vs. glTF ceiling) lives in
`M2_COMPLETENESS.md`, with the same breakdown for WMO + ADT (combined, not
yet implemented — a target-setting scaffold, not a progress report) in
`WORLD_COMPLETENESS.md`. The standalone Python corpus-exploration scripts
under `tools/` (independent-of-husk "second opinion" verification, plus a
generalized parallel corpus-scan framework) are documented in `TOOLS.md`,
not here — that's tooling, not husk's own export architecture.

## Goal

A real Blender import path for modern (Legion+, chunked) WoW M2 models —
mesh, skeleton, textures, materials, animation — via a correct glTF 2.0
export (`.glb`, core PBR metallic-roughness) that Blender's own built-in
importer opens unmodified. husk's job stops at "read WoW formats, write
correct glTF"; Blender-side concerns (addon UI, live reimport) are out of
scope unless glTF itself proves insufficient.

Non-goals, by design, not oversight:

- No *live* CASC/listfile access, ever, and no CASC-tool dependency. husk
  never resolves a FileDataID to a real WoW install path, and never links
  against or shells out to a CASC/DB2 client library. A separate tool
  (e.g. `casc-tool`) extracts real files to local disk first; husk only
  reads what's already there, and sidecar resolution
  (`--skin-dir`/`--textures`/`--anim`/`--bones-dir`) is a **local-directory,
  FileDataID-named** convention the user populates themselves — never CASC.
  **This does not extend to DB2 data itself once it's already on disk.**
  Scope clarified directly by Luna (2026-08-08, `CHAR_TEXTURE_COMPOSITING_
  TODO.md`'s own Background section has the full exchange): "the only hard
  boundary is not loading casc tool as a dependency... all data in
  wow_export is free for all, to be used." A raw `.db2` file already
  extracted to a local directory (the same `casc-tool`-populated tree as
  the `.m2`/`.skin`/texture files husk already reads) is exactly that —
  already on disk, same tier as every other sidecar — so husk parsing the
  WDC5 *file format* locally, or resolving a real customization-choice
  chain from those local files, is in scope and does **not** violate this
  non-goal; only talking to CASC/DB2 *live*, at runtime, does. (Earlier
  revisions of this file used looser wording here — "husk has no DB2
  access," "never will" — that predates this clarification and should be
  read as superseded wherever it's quoted or paraphrased elsewhere in this
  file; the up-to-date framing is this bullet.) An out-of-band tool
  scraping *live* CASC/DB2 at build time to learn something husk itself
  structurally can't (e.g. which `.bone` slot a customization choice
  selects, `TODO/TODO_correctness.md` #6) is likewise fine — it would just hand
  husk a plain local file/flag to read, same as every other sidecar. What
  husk itself never does, at runtime, under any circumstance, is talk to
  *live* CASC/DB2 directly, or depend on the CASC tool itself. A real WDC5
  DB2 parser now exists on top of this clarified scope (`src/db2.hpp`/
  `.cpp`, `husk db2-info`) — header/section/field-storage layout verified
  byte-for-byte against real files, real UTF-8 strings round-tripped from
  `namesreserved.db2`, confirmed by Luna directly. Real column naming is
  also implemented now (`src/dbd.hpp`/`.cpp`, an independent parser for
  WoWDBDefs' own documented `.dbd` grammar — optional, local-only, never a
  hard dependency, same tier as every other sidecar convention above) and
  exposed via `husk db2-export`, a real DB2-to-SQLite converter — but that
  command is an explicitly separate side project for human inspection/
  correctness-checking, not part of `export`'s own runtime path (`export`
  still doesn't read DB2 data). Still not a Stage-2+ consumer in the sense
  that matters for the real pipeline (no real per-table C++ struct feeds
  `export_materials.cpp` yet) — see `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s
  Stage 1 for the exact current-vs-target line, and README.md's own
  `husk db2-info`/`husk db2-export` sections for usage.
- **This same clarified scope extends to a local listfile too.** A real
  130,576-file corpus render pass (2026-08-09) found that a real extraction
  commonly keeps files under their own real name/path (e.g.
  `world/goober/bubble.blp`), not renamed to a bare `<FileDataID>.{blp,png}`
  -- cross-referencing against a real `community-listfile.csv` snapshot
  showed 99.9% of what first looked like "missing" FileDataID textures were
  actually present elsewhere in the tree under their real name (written up
  in `casc-tool`'s own `FAILURES.md`, item 13, as the finding that motivated
  this). A local `.csv` listfile snapshot is architecturally identical to
  the WoWDBDefs checkout the DB2 clarification above already covers:
  optional, user-supplied, never fetched/generated by husk itself, same
  "already on disk, never live CASC" tier as every other sidecar
  convention. `husk export --listfile <path>` (`src/listfile.hpp`/`.cpp`)
  loads one and uses it as a last-resort FileDataID -> real-name lookup,
  tried only after the exact `<FileDataID>.{blp,png}` convention already
  fails -- still deterministic (real data, not a guess), so it's tried
  before the fuzzy same-basename pool. This does **not** mean husk gained
  listfile awareness by default: unset (the default), behavior is
  byte-for-byte identical to before this flag existed.
- No write-back to WoW's native formats. glTF-out only.
- No WMO or M3 support implemented yet. WMO is tracked and now fully
  investigated/planned (not started in `src/`) — see `TODO/WORLD/WMO_GEOMETRY_TODO.md`/
  `TODO/WORLD/WORLD_PLACEMENT_TODO.md`/`TODO/WORLD/COLLISION_CULLING_TODO.md` and the rest of the
  Open work pointer below. M3 remains genuinely untracked, no investigation
  started: a full-storage `M3DT`-magic scan (casc-tool, product `wow` build
  68887, 1,891,552 files) found 8 real `.m3` files exist in this corpus
  (unresolved listfile names, `models\unknown\unk_exp*\<fdid>.m3`) — noted
  for the record, still out of scope.
- No ADT (terrain) support yet either (tracked, not started, `README.md`'s
  format matrix gained an ADT column 2026-07-31) — genuinely missed when
  this project was first scoped, not a deliberate exclusion: crucial for
  any actual rendered world, not just props/buildings. `.wdt`/`.wdl` (which
  ADT tiles exist per map; coarse whole-continent distant heightmap) are
  ADT's own real dependencies, not yet scoped either. **WMO and ADT are
  scoped together, not separately** — `WORLD_COMPLETENESS.md` (2026-07-31,
  a pre-implementation scaffold; expanded into eleven implementation-ready
  `*_TODO.md` companion documents 2026-08-01 after a real corpus
  investigation pass, see Open work below — still no code yet either
  format) covers both plus `.wdt`/`.wdl`, since ADT's whole relevance to husk is
  placing M2/WMO instances into world space (`MDDF`/`MODF`) and WMO has
  the identical concern turned inward (its own internal doodad set,
  `MODS`/`MODN`/`MODI`/`MODD`) — the two will get implemented as one
  effort, not two.
- **PM4/PD4 (server-side navigation/pathing mesh) declared in scope**,
  2026-07-31, explicitly for pathing use cases ("we want pathing").
  Genuinely different in kind from every other format above: never
  touched by the client renderer, only by server-side movement/AI, so
  "renders correctly" isn't the bar for it the way it is for M2/WMO/ADT —
  but (corrected 2026-08-01, `LUNA_NOTES.md`) that doesn't mean "excluded
  from export," it means "real geometry, hidden by default," the same
  spirit as M2's own collision mesh. Now has a full investigation and
  implementation plan, `TODO/WORLD/PM4_PD4_TODO.md` (see Open work below) — including
  a genuine structural wall found this pass: these files are never shipped
  to the client at all (a full live-storage sweep found zero real `.pm4`/
  `.pd4` files anywhere), not an extraction-completeness gap.
- **Shader bytecode (`BLS`/`GFAT`, WoW's compiled-shader files) investigated
  and deliberately deprioritized, not ruled out on principle.** Luna's own
  real investigation (not this session's) found actual intermediary
  compiled shader files — `.dxbc` (DirectX bytecode) and `.spv` (SPIR-V),
  both stripped of debug symbols/comments/cross-references, sitting at
  `wow_modding/export/shaders/` — confirming decoding them is *possible*,
  not just theoretical. Would answer a real correctness question this
  project can't currently answer any other way (Blizzard's actual LOD-
  dithering math), but the effort-to-visual-fidelity-gain ratio is judged
  too poor to prioritize right now, at this stage of the project. Revisit
  if/when core M2/WMO/ADT coverage stops being the bottleneck on visual
  fidelity.
- No Blender addon. A `.glb` file Blender's stock importer can open is the
  entire deliverable.

## Pipeline / module map

```
file bytes
   │
   ├─ src/chunk.cpp     generic tag-agnostic chunk reader (4-byte tag,
   │                    NOT byte-reversed — see Design Decisions)
   │
   ├─ src/m2.cpp/.hpp   MD20/MD21 header (fixed-offset memcpy, not a
   │                    packed struct), vertices, bones, sequences,
   │                    materials, attachments/events/lights, ribbons,
   │                    particles; M2Track/FBlock resolution (per-sequence,
   │                    global-sequence, constant-value, external-blob
   │                    splicing)
   │
   ├─ src/skin.cpp/.hpp .skin sidecar: triangle-index lookup, submeshes,
   │                    batches (material/texture linkage) — same
   │                    fixed-offset/bounds-checked approach as m2.cpp
   │
   ├─ src/skel.cpp/.hpp .skel sidecar (external skeleton): SKB1 bones /
   │                    SKS1 sequences, both handed straight to
   │                    m2::parseBones / m2::parseSequences — no parallel
   │                    struct implementation (see Design Decisions)
   │
   ├─ src/bone.cpp/.hpp .bone sidecar (per-bone correction matrices),
   │                    reverse-engineered, no wowdev.wiki page exists
   │
   ├─ src/gltf.cpp/.hpp glTF assembly — builds a tinygltf::Model, delegates
   │                    actual .glb binary framing to tinygltf
   │                    (WriteGltfSceneToStream), never hand-rolled
   │
   ├─ src/cmd_export.cpp   husk export: wires the above into gltf::Model
   ├─ src/cmd_info.cpp     husk info: header/record-count/chunk-tag summary
   └─ src/cmd_dump.cpp     husk dump-chunks: JSON dump of chunks with no
                           glTF equivalent, or .bone files directly

blp/  (separate Python/uv package, husk-blp CLI)
   BLP2 → PNG: hand-rolled container (header + mip table), but block
   decode (DXT1/DXT3/DXT5) delegates to Pillow via a synthetic DDS
   wrapper — not reimplemented color/alpha math.
```

Two file-shape branches funnel into the same parser at the earliest
possible point: pre-Legion flat `MD20` and Legion+ chunked `MD21` both
resolve to "one MD20-shaped blob starting somewhere, offsets relative to
that start" before `m2::parseHeader` ever runs — the branch is "where does
the blob start," not "how is the blob read."

## Data flow (husk export)

1. Read model file → detect flat vs. chunked → resolve MD20 blob.
2. Parse header (fixed offsets) → arrays: vertices, bones, sequences,
   materials, textures, textureCombos, attachments/events/lights,
   ribbons, particles, sidecar FileDataID chunks (SFID/AFID/BFID/PFID/
   SKID/TXID).
3. Resolve triangle indices + submesh/batch structure from the given (or
   `auto` + `--skin-dir`, FileDataID-resolved) `.skin` file.
4. If inline `bones` is empty and an `SKID`/`.skel` path is available,
   resolve bones (and sequences) from the `.skel` file instead — same
   structs, different blob.
5. Build the glTF skin (joint hierarchy, inverse bind matrices from bone
   pivots — no baked rotation/scale in M2's bind pose, so no matrix-chain
   composition needed) if bones exist; otherwise an unskinned mesh.
6. Per batch: submesh slice → material → textureCombo → texture entry →
   glTF material (`alphaMode`/`doubleSided` from WoW blend mode/render
   flags; `baseColorFactor` from a *constant-only* M2Track resolution of
   color/alpha; `baseColorTexture` embedded only if `--textures <dir>` has
   a matching `<FileDataID>.png`).
7. Per sequence: resolve per-bone translation/rotation/scale M2Tracks —
   inline (`flags & 0x20`) from the owning blob, or external (no `0x20`,
   no `0x40`) from a `--anim`-resolved `.anim` file via the model's (or
   `.skel`'s own separate) `AFID` table. Z-up→Y-up conversion applied to
   every keyframe (see Design Decisions). `0x40` ("alias") sequences are
   skipped — wowdev.wiki itself doesn't know where that data lives.
8. Write one glTF `animation` clip per sequence that actually resolves to
   non-empty keyframe data.
9. `--lod all`: repeat steps 3/6 per `SFID` entry, but build **one** shared
   skeleton/skin/animation set (`gltf::writeGlbMulti`) — LOD only changes
   which vertices/triangles are selected, never bind pose or animation.

## Key design decisions

**Chunk tags are read literally, not reversed.** M2 is the odd one out
among WoW's chunked formats (WMO/ADT reverse chunk tag bytes) — a chunk
written `MD21` on disk is matched against the literal string `"MD21"`.
Getting this backwards is a classic WMO/ADT-experience trap.

**Header/track/sequence fields are read via explicit, bounds-checked
`memcpy` at named offsets — never a packed C struct.** The header's tail is
version/flag-conditional, and relying on compiler layout for that is
fragile and silently wrong across versions.

**Every top-level M2 chunk tag encountered is tracked and diffed against a
known-tag list, because this format keeps growing new chunks.**
`readChunks` is tag-agnostic by construction (unknown tag → skipped, never
an error), but "doesn't break" isn't the same as "someone would notice."
`Header::chunkTags` + `husk info`'s `documentedM2ChunkTags` turn an
unrecognized tag into an explicit `note:` line, and
`tests/test_integration.cpp` makes that a live canary against real,
re-extracted game data. See the README's Design notes for the recurring
*shapes* new chunks tend to take (FileDataID indirection, small
feature-gated additive chunks, feature-correlated clusters, new-tag-not-
version-bump revisions) — useful for anticipating the next one before it
exists.

**`.skel`/`.bone` sidecars reuse `m2::parseBones`/`m2::parseSequences`/track
resolution outright, never a parallel implementation.** A `.skel`'s `SKB1`
bones and `SKS1` sequences are the *identical* structs the M2 itself uses,
just at offsets relative to a different blob — verified (not assumed) by
checking a real `.skel`'s bone-track outer-array count against its own
`SKS1` sequence count before trusting the reuse. This is a hard constraint
on future work: a new sidecar carrying an M2-shaped struct should be wired
into the existing parser via a different blob/offset base, not given its
own copy.

**`M2Track` constant-value resolution requires exactly one sub-array with
exactly one keyframe — not element `[0][0]` unconditionally.** Reading
`[0][0]` blindly (matching wowdev.wiki's own worked example) once read a
genuinely per-sequence-animated alpha track's *sequence-0* keyframe as if
it were a static default, which for one real batch was `0` (fully
transparent) — silently invisible model. `constantTrackValueOffset` is the
fix; the regression test in `tests/test_m2.cpp` is named after this bug.
Any future "resolve this M2Track as a static value" code must go through
the same both-counts-exactly-1 check, not a shortcut.

**External `.anim` files: per-sequence data is spliced across two blobs, not
duplicated.** wowdev.wiki's only description ("just a blob of data...
pointed to by the first array_ref layer") means the M2's own per-sequence
inner `M2Array` descriptor stays real and non-zero for an external
sequence — only its `offset` is relative to the `.anim` blob instead of the
M2's. `resolveVec3TrackSequence`/`resolveQuatTrackSequence`'s
`externalDataBlob` parameter implements exactly that split: descriptor from
the owning blob, payload bytes from whichever blob is in play.

**`.anim` files carrying an `AFSB` chunk resolve real per-bone data, the same
way as the `AFM2`-external case just above.** For `.skel`-sourced models,
real per-bone data lives in `AFSB`, not `AFM2` — a small, always-near-zero
`AFM2` "stub" can be present alongside it and must not be mistaken for real
track data (confirmed via a real bounds error when tried anyway), so `AFSB`
takes priority whenever both are present. `AFSB`'s own byte layout has no
published spec anywhere (`WIKI_FINDINGS/M2/anim.md`'s follow-up has the full
reverse-engineering writeup), but it turned out not to need one: `SKB1`'s
own per-bone, per-sequence `M2Track` `(count, offset)` descriptors — the
exact same ones the paragraph above already resolves for `AFM2` — point
directly into the `AFSB` payload's own byte range for external sequences
too. `buildAnimations` just extracts `AFSB`'s payload as the
`externalDataBlob` instead of an `AFM2` blob; `resolveVec3TrackSequence`/
`resolveQuatTrackSequence` needed no changes at all. Verified against the
entire real 104-file `bloodelffemale_hd_*.anim` corpus (zero
bounds/monotonicity/finiteness problems) and, end to end, three independent
ways: husk itself (336 real clips), the Khronos `gltf_validator` (no new
errors), and Blender's own glTF importer running headlessly (336 actions,
matching exactly). That 336-clip verification checked the `AFSB`/`AFM2`
*decode* logic, not `--anim`'s own file-resolution: `--anim` used to look
only for `<FileDataID>.anim`, and a real `wow.export`-style extraction names
external `.anim` files `<model-basename><animId>-<subId>.anim` instead (the
committed `bloodelffemale_hd0069-00.anim`/`-01.anim` fixtures match this
exactly) — `findAnimFileByBasename` (`cmd_export.cpp`) now tries that
convention whenever a `<FileDataID>.anim` file isn't found (or there's no
`AFID` entry at all), so a real character-model extraction actually resolves
its external clips through `--anim auto`'s own default, not just through a
`.anim` directory hand-populated with FileDataID-named files.

**`M2Sequence` is 64 bytes, not 36** — the wiki's own struct listing has an
un-renumbered offset comment that reads as if `M2Bounds bounds` weren't
really there. Decoding all of a real file's sequences at both candidate
strides (not re-reading the prose harder) is what settled it. See
`WIKI_FINDINGS/M2.md`. This is the model for how to resolve any future
spec ambiguity here: decode real records at every plausible stride/shape
and check for garbage, don't guess from text alone.

**Bone rotation/scale keyframes get their own Z-up→Y-up conversion,
mechanically derived from the same matrix `zUpToYUp` uses for
positions/normals, not a separately hand-typed formula.** No wowdev.wiki
formula exists for this specific step; the general change-of-basis rule for
a proper-rotation basis change applies (a rotation quaternion's vector part
gets the same matrix's conjugation, scalar/`w` untouched; a scale vector
gets the same permutation with signs dropped). Historical note, corrected
below: an earlier hand-typed version of this rule, applied to an earlier
(wrong) position matrix, shared that matrix's own sign bug undetected for a
real stretch of active development — see the "Follow-up" entry near the end
of this document (`TRANSFORM_TRIAGE.md`) for the fix, and `gltf.hpp`'s own
doc comments for the current, corrected, single-source-of-truth
implementation. Still depends on the assumption that WoW bones use plain
parent-relative TRS with no separate pivot concept beyond
`M2CompBone.pivot` — unverified against a real animated pose in Blender's
own viewport (deliberately left to Luna, see the Follow-up entry).

**glTF output is never hand-rolled.** `src/gltf.cpp` builds a
`tinygltf::Model` and hands the actual `.glb` binary framing to
`TinyGLTF::WriteGltfSceneToStream`. `gltf.cpp` includes `tiny_gltf.h` and
links against the prebuilt lib; it must never define
`TINYGLTF_IMPLEMENTATION` itself (double-definition against
`libtinygltf.a`). What tinygltf does *not* do for husk is pad the shared
buffer it hands it -- `appendBufferView`'s `padTo4()` zero-pads up to the
next 4-byte boundary after every append (mesh data, embedded images,
animation sampler output), because glTF requires every accessor's total
byte offset to be a multiple of its component size and an embedded PNG's
byte length is essentially never itself a multiple of 4 -- without this,
a `--textures` export could misalign every bufferView appended after the
first embedded image, a real bug this project found and fixed (not a
hypothetical).

**`husk dump-chunks` is a deliberately separate output shape (JSON), not a
step toward richer glTF.** Chunks like parent-sequence overrides,
waterfall-shader constants, edge fade, per-particle side-data are real M2
data with no natural glTF material/animation slot — forcing them in would
mean inventing semantics the format doesn't have. Chunks with no
documented layout, or one the wiki itself flags uncertain, are still
emitted (hex dump + reason), never silently dropped — same principle as
the `M2Sequence`-stride lesson: undocumented isn't the same as safe-to-skip.

**LOD tiers share one skeleton/animation set by construction.** Every
`.skin` belonging to one M2 selects a *subset* of that M2's own global
`vertices`/`bones` — nothing about bind pose or animation differs per LOD.
`writeGlbMulti` builds one skin/joint hierarchy shared by every LOD mesh
node; `writeGlb` (single-mesh) is implemented as `writeGlbMulti` with one
entry, not maintained as separate code.

**Geoset selection and multi-texture-layer rendering are tagged via glTF
`extras`, never filtered or faked.** Two related gaps husk can't actually
close itself (not yet implemented, not structurally impossible -- see
Non-goals above's clarified wording: a locally-extracted `.db2` file is in
scope, husk just doesn't parse WDC5 or resolve customization chains yet,
`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`): *which* `M2SkinSection.skinSectionId`
variant is "correct" for a given character depends on DB2 data
(`CharacterSections`, geoset groups) husk doesn't currently read; and a
batch's additional texture
layers (`M2Batch.textureCount > 1`) exist to feed WoW's fixed-function
combiner math (`Mod2x`/`Add`/env-mapping), which has no core-glTF
equivalent to translate into. Rather than guessing a default geoset
selection or faking a `KHR_materials_*` extension that would misrepresent
what the combiner actually does, both are surfaced as inert `extras`
metadata instead -- every primitive carries its real `skinSectionId` (plus
a derived `geoset_group`/`geoset_variant` split), every material with
`textureCount > 1` carries its additional layer(s)' FileDataID/UV set
(plus a real embedded-but-unused image when `--textures` has a match) --
the same "tag it, don't guess at semantics" treatment `billboardMode`
already got for the same reason (billboarding is camera-relative, decided
by whatever engine/renderer is actually drawing the frame, not by husk).
A custom renderer or a Blender script (mesh mask, geometry nodes, a driven
material, ...) has everything it needs to implement its own selection/
blend on top of this data; husk exporting *every* geoset unconditionally
(with a loud note whenever a `.skin` spans more than one distinct
`skinSectionId`) and rendering only the first texture layer (with an
equivalent note) are the two honest fallbacks until that Blender-side
tooling exists.

**Global-sequence-driven bone tracks get their own glTF animation clips,
independent of `M2Sequence`.** A track whose `TrackMeta::global_sequence`
field isn't the "none" sentinel loops continuously against the model's own
`global_sequences` duration table (`Header::globalLoops`) rather than any
specific `M2Sequence`'s timeline (wowdev.wiki "Global Sequences": "always
loops") -- eye-glow pulses, torch flicker, idle sway. Per wowdev.wiki,
"blocks that use global sequences also only have one track": such a
track's outer `M2Array<M2Array<T>>` always holds exactly one sub-array
(index 0), the same shape `constantTrackValueOffset` already exploits for
`M2Color`/`M2TextureWeight`'s constant-value case, just with real
(possibly many-keyframe) animation data instead of a single value.
`resolveVec3GlobalSequenceTrack`/`resolveQuatGlobalSequenceTrack` resolve
that one sub-array directly; `cmd_export.cpp`'s
`buildGlobalSequenceAnimations` finds every distinct global-sequence index
a model's bones actually use and builds one clip per index
(`global_seq_<n>`), reusing the same per-bone `JointAnimation`-building
logic (`buildJointAnimation`) the per-`M2Sequence` path already used, now
factored out into a shared helper. This is intentionally *not* baked into
whichever `M2Sequence` clip happens to be playing -- a global-sequence
track loops on its own independent timeline no matter which (if any)
`M2Sequence` the rest of the skeleton is animating through, so folding it
into a per-sequence clip would misrepresent when it actually plays.

**A batch's `M2TextureTransform` (UV scroll/rotate/scale) gets a real
`KHR_texture_transform` for the constant case; the animated case stays
`extras`-only.** Core glTF's `KHR_texture_transform` extension is itself
*static* (offset/rotation/scale baked once, no animation-channel target),
so the animated case (almost always the common one in practice — scrolling
lava/water is the model's whole reason to carry one) has no honest
representation regardless of effort and stays `extras`-only, same "tag it,
don't guess at semantics" family as geoset selection/multi-texture-layer
rendering above. The constant case *is* wired to a real extension
(`gltf_mesh.cpp`'s `textureTransformToKhr`): wowdev.wiki notes M2's
rotation pivots around the texture's center (0.5, 0.5), not the
extension's own (0,0) origin, so a correct translation has to fold that
pivot difference into the extension's `offset` field — derived as
`offset = R*S*translation + R*t_S + t_R` (`t_S`/`t_R` = the pivot's
displacement under scale/rotation respectively), verified against real
`bloodknightcharger.m2` data three independent ways (hand computation,
20,000 randomized trials against `reference/wow.export`'s own
translate-rotate-translate matrix composition, and headless Blender's own
glTF importer producing an exactly-matching Mapping node). Applied only
when the transform is genuinely constant (every track either empty or a true single value — a real
`brewfestmount.m2` counterexample has a constant rotation but
per-sequence-structured translation/scaling tracks whose values just
happen to all be identity, and correctly stays extras-only, not falsely
"constant") and the rotation is planar (Z-axis only, the only case
wowdev.wiki's pivot note describes) with a real `baseColorTexture` to
attach the extension to. `m2::parseTextureTransforms` resolves the same
constant-vs-animated split `constantTrackValueOffset` already does for
`M2Color`/`M2TextureWeight`; `gltf::Material::textureTransform` carries the
raw values through to `extras` unconditionally too — a diagnostic, and the
animated case's only representation.

**A texture's `M2Texture::type` (nonzero -- a hardcoded/replaceable slot)
is surfaced as `extras`, distinguishing "husk can't resolve this locally"
from "the `--textures` directory just didn't have the file."** `type == 0`
("NONE") is a real, filename/FileDataID-based texture, resolvable the
ordinary way; any other value means the client substitutes the real image
at runtime from DB2-driven character-customization/item-tint data husk
doesn't fully resolve yet (locally-extracted DB2 files are in scope per
Non-goals above's clarified wording -- `husk export --db2-dir/--dbd-dir/
--char-layout-id` now attaches real placement-geometry `extras`
(`ChrModelMaterial`/`CharComponentTextureSections`/`ChrModelTextureLayer`,
`src/chrmodel_db2.hpp`), but *picking* which candidate fills which slot for
a given character is still unresolved -- see
`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 3) -- so an empty `baseColorImagePng`
for one of these means something categorically different than a missing
PNG for a `type == 0` texture. `gltf::Material::textureType` is set from
the batch's primary texture's `m2::Texture::type` unconditionally, but only
serialized into `extras` (`texture_type`) when nonzero -- 0 is the ordinary
case, matching `additionalTextureLayers`/`textureTransform`'s own "presence
means something extra to say" convention, rather than writing a redundant
key on every material.

**A batch's animated `M2Color`/`M2TextureWeight` gets its real curve
extracted as `extras`, never a native playback attempt.** The
global-sequence bone-track fix (above) doesn't have a material-side
*playback* counterpart: a bone's translation/rotation/scale are real,
independently animatable glTF node properties, but core glTF has no way to
*play back* an animated material property at all (unlike the
texture-transform case just above, this isn't even a question of extension
support — there's no core-glTF animation-channel target for a material's
`baseColorFactor`, full stop, so this stays permanently `extras`-capped
regardless of how much more husk parses). `m2::Color::colorAnimated`/
`alphaAnimated`/`m2::TextureWeight::weightAnimated` (via the shared
`trackHasAnimatedData` helper) distinguish "nullopt because genuinely
empty" from "nullopt because it's animated," and `cmd_export.cpp`'s
`resolveAnimatedColorCurve`/`resolveAnimatedFixed16Curve` resolve the real
keyframe data the same per-sequence/global-sequence way `buildAnimations`
already does for bones (`resolveVec3TrackSequence`/
`resolveRawIntTrackSequence`, the latter since `M2Color::alpha`/
`M2TextureWeight::weight` are `M2Track<fixed16>`, not `M2Track<float>` —
reusing `resolveFloatTrackSequence` here would misread the wire bytes).
The resolved curves attach as `gltf::Material::tintAnimation`/
`alphaFadeAnimation`/`weightFadeAnimation` → `tint_animation`/
`fade_animation` (`"alpha"`/`"weight"` sub-keys) material `extras`, one
entry per M2Sequence that has real inline data plus a synthetic
`sequenceIndex == -1` entry for a global-sequence-driven track — the same
"tag it, don't guess at playback" treatment the texture-transform case
gets, for a custom renderer or Blender script to apply itself.
`cmd_export.cpp` still prints `animatedTintOrFadeBatchCount`'s note (now
naming the extras keys instead of saying the data is dropped).

**`.bone` correction data is surfaced as `extras`, never applied to the bind
pose.** Same "tag it, don't guess at semantics" family as geoset selection/
texture-transform above, for the same underlying reason: which of a model's
several `.bone` files (its `BFID` array) is "correct" for a given character
is selected by client-side customization-choice data (a DB2-shaped lookup)
husk doesn't currently resolve (locally-extracted DB2 files are in scope
per Non-goals above's clarified wording, just not implemented yet -- the
same real gap `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` is closing for texture
compositing could, in principle, extend here too). Real
investigation (`WIKI_FINDINGS/BONE.md`'s follow-up) ruled out the two more
tractable-looking hypotheses first — LOD/render-distance (the slot count
doesn't fit the model's real LOD tier count, and slots collapse into far
fewer distinct bone-index sets than a detail ladder would produce) and
weapon-type/armor-type (the corrected bones cluster on the Head/Jaw, not
the hand/wrist bones a grip correction would need) — before concluding the
real selector is external, unreachable data, not a gap husk itself could
close with more file-reading. `husk export --bones-dir <dir>` therefore
resolves every `BFID`-declared FileDataID it can find on disk (same
local-directory, never-CASC convention as `--textures`/`--skin-dir`/
`--anim`) and attaches every one it finds as a `gltf::Skeleton::
CorrectionSet` — real `(bone_index, matrix)` data, faithfully surfaced —
serialized as `bone_correction_sets` on the glTF skin's `extras`, for a
downstream renderer or Blender script that *does* have the slot-selection
mapping to apply on top.

**Ribbons/particles: same `extras`-only family as geoset/texture-transform/
`.bone`-correction, but split across two destinations instead of one,
because of data volume.** `M2Ribbon`/`M2Particle` (`m2::parseRibbons`/
`parseParticles`, `src/m2.cpp`) are procedural emitter systems — no core
glTF primitive represents a particle emitter or a ribbon trail generator,
so like the cases above, the real answer is "tag it, don't guess at
semantics." What's different here is scope: a `.bone` correction set is one
small `(bone_index, matrix)` list per file; a model can have dozens of
particle emitters, each with up to ~15 animation curves once fully
resolved. Cramming all of that into every `.glb`'s `extras` — the way
`bone_correction_sets`/geoset/texture-transform data already does — would
bloat a file most consumers (a plain glTF viewer, a renderer that doesn't
care about VFX) can't use any of. So the data is deliberately split:
`husk export` attaches only a **minimal placement anchor** (`id`/`boneIndex`
/`position`, `gltf::Skeleton::EmitterAnchor`) to the skin's `extras` — enough
for a Blender script to place a marker at the right bone — and the **full
record** (every field, every resolved curve) goes to `husk dump-chunks`'s
JSON output instead, which has no such size pressure (see `src/cmd_dump.cpp`'s
doc comment for how that command's own scope broadened from Legion+-chunk-
tags-only to cover these two core header arrays too, since the "no glTF
slot" rationale that already justified the chunk-tag side of that command
applies just as much to the parsed record itself). `M2Particle`'s own byte
shape is genuinely version-conditional back to pre-BC; real weapon data was
only available for Cataclysm+ (272), so parsing is gated to
`kMinVerifiedParticleVersion`, the same "verified floor, warn below it"
policy `kMinVerifiedRecordStrideVersion` already established for Bone/
Sequence/Ribbon — see `WIKI_FINDINGS.md` for the real-file cross-check (a
real weapon's particle color curve decodes to an actual fire/ember
gradient, alpha curves are clean fade envelopes) that this offset table and
the FBlock/M2Track curve-resolution split (FBlock curves are flat, not
per-sequence — "unable to change between different animations," per the
wiki — while `M2Track`-based ones are, resolved per `M2Sequence` or via the
global-sequence resolver) were both confirmed against.

**`.phys` physics/collision data follows the same minimal-anchor/
full-record split as ribbons/particles, for the same volume reason.**
`.phys` is a sidecar file (like `.bone`, not a core M2 header array like
ribbons/particles), but its content structurally resembles ribbons/
particles more than `.bone`: `BODY`/`BDY3`/`BDY4` records are already
`position + boneIndex` anchors, not a flat correction-matrix-per-bone table.
And the volume is real — a single real file can have 40+ bodies, each with
several shapes/joints, more than `bone_correction_sets` ever carried.
`husk export --phys` (three-state, mirroring `--skel` — `PFID` is a single
scalar FileDataID, like `SKID`, not an array like `BFID`/`AFID`/`SFID`, so
no directory flag) attaches only a **minimal placement anchor** (`id`/
`joint`/`position`/`bodyType`, `gltf::Skeleton::PhysicsBody`) to the skin's
`extras`; the **full record** (every body/shape/joint/`PHYV` field,
resolved) goes to `husk dump-chunks`'s JSON output instead, which also
accepts a `.phys` file directly, same as `.bone`. Unlike `.bone`, `.phys`'s
byte layout is genuinely documented (`documentation/wowdev-wiki/md/
PHYS.md`), not reverse-engineered — `src/phys.hpp`/`phys.cpp` implement that
spec directly, verified against 103 real files with zero cross-chunk index
errors (`WIKI_FINDINGS/PHYS.md`). Chunk tags are byte-reversed on disk (WMO/
ADT convention), the opposite of M2's own inline chunks — `chunk.hpp`'s
`readChunks`/`findChunk` are reused as-is, just called with the
already-reversed literal.

**The collision mesh is real exported geometry, not inert extras — the one
place this differs from the geoset/texture-transform/`.bone`-correction
family above.** Those all became `extras` because no unambiguous glTF
translation exists (fixed-function texture combiner math, a customization
slot selector with no local data to resolve it); a collision mesh is just
a plain indexed triangle mesh with an obvious translation, so
`cmd_export.cpp` writes one (`m2::parseCollisionMesh` → one more
`gltf::NamedMesh`). The only `extras` use
here is a `{"collision": true}` tag on that mesh's node, purely to mark
*purpose* (so a renderer/Blender script knows not to draw it) — the
geometry itself is native. It's deliberately unskinned even though it
shares the render mesh's skeleton (a collision mesh is static, not
deformed by the armature): `gltf::writeGlbMulti` previously required every
`NamedMesh` entry to be skinned whenever *any* shared skeleton was in
scope, so supporting this needed a real relaxation — each entry now
independently opts in (non-empty, matching-length `mesh.skinning`) or out
(empty, no glTF `skin` reference on that node) rather than an all-or-
nothing rule across the whole call. Per-vertex normals are *approximated*
(averaged adjacent face normals) since the M2 source only has one normal
per triangle, not per vertex — acceptable because a collision mesh isn't
shaded, this is only to satisfy `gltf::Mesh`'s own same-length invariant
with real, finite data rather than a placeholder.

**Off by default, opt-in via `--collision`.** Originally shipped
included-by-default with `--collision none` as an opt-out; flipped after a
real headless-Blender render of `bloodelffemale_hd.m2` came out looking
like a plain white slab — the collision hull (a coarse capsule/box, often
larger than the character it approximates) was sitting in the scene,
untextured, occluding the entire render, exactly as the `{"collision":
true}`-tag caveat above always warned it could. A tag a *script* can filter
on doesn't help someone who just opens the file and looks. Full body/shape/
joint records remain available either way via `husk dump-chunks`.

**BLP2 texture decode is embedded directly in husk, in-memory, no `husk-blp`
invocation needed.** Reversed from an earlier "permanently a separate
Python process" stance once that stance itself broke husk's other goal of
being a single tool usable standalone — running `husk-blp` first, in the
right order, before `husk export` isn't that. The two Pillow dependencies
that motivated the original split turned out to have easy from-scratch C++
answers already sitting in this project's own build: DXT1/DXT3/DXT5 (BC1/
BC2/BC3) block decode is small, fully public, deterministic math, hand-
rolled in `src/blp.cpp` the same way every other small binary format here
is; PNG encoding (`stbi_write_png_to_mem`) ships inside the `tinygltf`
package husk already links (`libtinygltf.a` already compiles it in — no new
dependency, see `src/blp.cpp`'s own doc comment for why its prototype is
declared by hand instead of via a second `#include`). `--textures` now
resolves a `.blp` candidate exactly like a `.png` one
(`src/export_texture_resolution.cpp`'s `resolveTextureBytes`/`readTextureFileBytes`),
decoding and re-encoding entirely in memory — nothing is written to disk
unless `--textures-out` is explicitly given. `blp/`'s Python tool
(`husk-blp`) is kept, unmodified, as the independent ground truth the C++
decoder's tests are checked against (`tests/test_blp.cpp` ports its
fixtures directly) — not retired, since it predates this decoder and gives
an independent second implementation to catch a divergence in either one.

**A genuinely geometry-less model gets zero mesh nodes, not an empty one —
"zero meshes," not "one empty mesh."** A 130k-file corpus sweep
found 3,807 real files (pure particle/ribbon VFX
models, e.g. `particles/lootglow_boss.m2`) with 0 vertices at the M2 level,
which `buildMaterialsAndPrimitives` used to turn into one glTF primitive
with empty `indices` — a shape glTF itself has no valid representation for
(a primitive's `indices` must be non-empty when present at all), so every
one of these failed outright. The fix skips adding a `NamedMesh` for a LOD
tier that resolves to zero primitives (both the whole-file-empty case and
the rarer per-submesh `indexCount == 0` case) rather than manufacturing
one; `gltf::writeGlbMulti` now accepts an empty `meshes` list as long as a
real skeleton (at least one joint) exists to fall back to — the model's
skeleton and ribbon/particle emitter anchors (already unconditional, see
above) still export, just with no mesh in the document at all. `Error`s
outright if both are empty (nothing to export) rather than silently
producing a degenerate `.glb`.

**A same-basename numeric-suffix `.skin` match prefers exactly 2 digits
when one exists.** `findSameBasenameSkins`'s digit-suffix scan used to
accept any digit-run length, which a real corpus scan found genuinely
ambiguous whenever one model's basename is itself a
numeric-suffix prefix of a sibling model's basename in the same directory
(`mogu_library_crate_10.m2` vs. `mogu_library_crate_1.m2` — the shorter
model's own `...1` + `00` LOD suffix parses as a spurious 1-digit match
for the longer model's `...10` basename too, and used to win the
lexicographic tie-break over the correct `...10` + `00` file). WoW's own
convention is always exactly 2 digits, so the scan now discards
1-digit/3+-digit matches whenever at least one 2-digit match exists for
that basename — kept as a fallback (not a hard reject) when no 2-digit
match exists at all, since that shape has no real-corpus evidence either
way and a hard reject risks a new false-negative regression for it.

**A duplicate animation keyframe timestamp is repaired, not rejected.**
`i > 0 && keyframes[i].first <= keyframes[i-1].first` used to throw
unconditionally — correct for genuine disorder (a timestamp that actually
*decreases*, real corruption), but a real corpus scan (5 files) found an
*exact*-duplicate timestamp is real, shipped
Blizzard data: a "hard cut" pose authored as two rotation keyframes at the
same instant. Collapsing either keyframe would silently discard one of the
two real authored values; the fix instead nudges the later duplicate's
timestamp forward by 1ms (cascading, so a run of N duplicates spreads out
N-1ms apart), classified against each keyframe's *original* (pre-repair)
timestamp so the cascading case doesn't misfire the disorder check against
an already-nudged value. A timestamp that's genuinely less than the
previous one still throws — repairing that would mean guessing which of
the two conflicting values is "right," not resolving a known, consistent
shape.

**A multi-root M2 bone forest gets one synthesized, non-joint glTF parent
node — never a fake extra joint, never a filter that drops real bones.**
WoW's engine never required a single bone tree; a corpus-wide sweep
(`tools/find_multiroot_skeletons.py`) found
**35% of a real 130k-file corpus** (45,804 files) has more than one root
bone (`parentBone == -1`) — common, intentional M2 data (particle-emitter
anchors, accessory bones), not corruption. Core glTF's skinning model and
tooling built on it (`gltf_validator`'s `SKIN_NO_COMMON_ROOT`) generally
expect one common root, though empirically the validator catches only a
small, unexplained slice of the full 35% (a 150-file random sample found
just 11 currently flagged — raw root count and vertex-weighted root count
both fail to predict which files trigger it). `writeGlbMulti` now
synthesizes one plain `tinygltf::Node` (default/identity transform, never
added to `skin.joints`, never given an inverse bind matrix) as the parent
of every real root joint whenever `rootJointNodeIndices.size() > 1`,
appended past the end of the joint-node range and set as the sole
`scene.nodes` entry standing in for those roots (`skin.skeleton` points at
it too) — the shape glTF's own tooling ecosystem already anticipates for
multi-rooted skeletons (Khronos discussion, github.com/KhronosGroup/glTF/
issues/1270). `Skeleton::joints` itself is never touched, reordered, or
added to: every vertex/emitter-anchor/correction/animation joint index
stays a raw, unremapped M2 bone-array index — the one invariant this fix
must never break, per `src/gltf.hpp`'s `Skeleton` doc comment — a synthetic node inserted into
`Skeleton::joints` instead would silently misattribute every one of those
consumers, with no crash and no validator error). Verified empirically,
not just by spec-reading: Blender's own glTF importer, run headlessly
against a real 15-bone/10-root weapon fixture, reports `bone_count`
matching the real M2 bone count exactly — it does not count the
synthesized node as a bone. Single-root models (the overwhelming
majority) are completely unaffected: no synthetic node, `skin.skeleton`
left unset, byte-identical output to before this existed. Two other
options were surveyed and rejected: appending the synthetic node as one
more real joint (an identity-IBM "Armature bone," matches some other
exporters' convention, but grows `skin.joints` past `header.bones.count`
for no fidelity gain) and filtering non-hierarchical bones out entirely
(`wow.export`'s apparent approach — a real, community-precedented option,
but drops real M2 bones from the glTF output, against this project's own
1:1-fidelity goal).

**Attachments/Events/Lights (`M2Attachment`/`M2Event`/`M2Light`) become real,
plain child glTF nodes — unlike every other placement-anchor list
(`EmitterAnchor`/`PhysicsBody`), which stay skin `extras`.** These three
differ from ribbons/particles/`.phys` bodies in one concrete way: a
bone-relative position marker is *all* their static data ever is — no
blend mode, no texture, no curves left over that `extras` would still need
to carry. `M2_COMPLETENESS.md` used to call this "node-possible,
unclaimed"; a plain glTF node is a full, lossless translation of that data,
so `writeGlbMulti` now builds one real `tinygltf::Node` per entry instead
of another anchor list on the skin's `extras` object. Each node is
translation-only (no rotation/scale — the same convention a joint node's
own `.translation` already uses) at `Attachment/Event/Light::position`,
named `attachment_<id>`/`event_<identifier>`/`light_<index>` (`M2Event
::identifier` is not deduplicated — a real file can repeat one, e.g. six
"$CSD" sound-cue events on one weapon — duplicate glTF node names are
legal, so this isn't a problem), and parented as a `.children` entry of its
owning joint node — appended past the joint-node range (and past the
synthesized multi-root node, if any), attachments first, then events, then
lights, and **never added to `skin.joints`** — the same invariant the
multi-root synthesized parent node above must never break, since every
vertex/emitter-anchor/correction/animation joint index is still a raw,
unremapped M2 bone-array index. Verified empirically the same way the
multi-root fix was: Blender's own glTF importer, run headlessly against a
real weapon fixture (5 attachments/2 events/4 lights, 78 real bones),
reports `bone_count == 78` — the anchor nodes are not counted as bones —
and the Khronos `gltf_validator` reports zero errors on the same export.
`bone == -1` ("not attached to any bone," real per wowdev.wiki for
`M2Light`, and plausible for `M2Attachment`) is treated like any other
out-of-range joint and throws — husk has no established "unparented
placement node" concept yet, and no real fixture seen so far exercises
this case, so it's flagged rather than guessed at (see `src/gltf.hpp`'s
`Skeleton::Attachment`/`Event`/`Light` doc comments). `M2Light`'s `type`
and every animated field (color/intensity/attenuation/visibility) stay out
of scope — a placement node has no slot for either, and the animated
tracks are a separate, larger problem.

**`M2Sequence`'s own metadata (movespeed/frequency/replay/blendTime/bounds/
variationNext/aliasNext) is per-clip glTF `extras`, and `aliasNext` is now
chain-resolved into a real animation clip** (`WIKI_FINDINGS/M2.md`).
`m2::Sequence` gained the seven fields wowdev.wiki
documents beyond `id`/`variationIndex`/`duration`/`flags`, at the exact
offsets that fit cleanly into the already-verified 64-byte stride (see
`Sequence`'s own doc comment) — no ambiguity, straightforward parse.
`aliasNext` is the one with real behavior attached: it's a plain local index
into the same file's own `sequences` array, not an `AnimationData.dbc` id
(§12) — `buildAnimations` (`cmd_export.cpp`) used to skip every
`flags & 0x40` ("is alias") sequence outright, citing the wiki's own
now-superseded "I have no clue." It now resolves the chain
(`resolveAliasChain`, bounded to `sequences.size()` hops, throwing rather
than looping forever on a cycle real data has never shown) to the terminal
non-alias sequence and reuses *that* sequence's own keyframe data — inline or
external, whichever it uses — registered under the *alias's own* id/index, so
a real clip appears where none did before. **One real priority subtlety a
committed real fixture (`bloodelffemale_hd.skel`) forced, not anticipated by
the original plan**: 31 of its 38 real alias sequences *also* carry
`flags & 0x20` ("stored inline") — i.e. they already have real inline
keyframe data of their own and don't need (or want) another sequence's data
substituted. `0x20` wins priority exactly the way it did before `aliasNext`
resolution existed (checked first, alias-chain resolution only triggers for a
"pure" alias — `0x40` set, `0x20` not) — resolving the chain unconditionally
for every `0x40`-flagged sequence would have silently changed those 31 real
clips' content, a regression caught by re-deriving the fixture's own flag
bytes directly (not assumed) once a real export's animation count didn't
change after the fix, as expected (see below). The other 6 non-`aliasNext`
fields (movespeed/frequency/replay/blendTimeIn/blendTimeOut/bounds) have no
core-glTF clip-level equivalent (playback speed scaling, blend time, replay
count, bounding volume) — exposed as `sequence_metadata` extras on the clip
itself (`gltf::Animation::SequenceMetadata`), the alias's own field values
even when its keyframe data is borrowed, same "tag it, don't guess at
semantics" treatment `bone_correction_sets`/`ribbon_emitters` already get.
**A real, honest finding, not a guess**: against the exact fixture set
committed to this repo, the fix's *measured* effect on
`bloodelffemale_hd.m2`'s own animation count is **zero net new clips** —
of the 7 "pure" alias sequences, all 7 resolve (in the full real corpus) to a
terminal sequence needing an external `.anim` file, and none of those 6
distinct files happen to be among the ~104 already committed here (confirmed
by hand against the full corpus at `/media/luna/data/wow_export`) — the
original implementation plan's own "don't assume every alias necessarily
gains a clip without checking" caveat turned out to be exactly right for
this data. `tests/test_cli.cpp`'s synthetic fixtures (2-hop, multi-hop,
both-flags-priority, self-referencing cycle, out-of-range `aliasNext`) prove
the mechanism itself works; `tests/test_integration.cpp`'s real-fixture case
pins the "zero net growth here, and it doesn't crash on the 7 unresolvable
ones" finding directly.

**`PCOL` (player-housing collision, War Within 11.1.7+) is diagnostic-only
(`husk dump-chunks`), no glTF slot** — same class as `EXP2`/`PFDC`/`DETL`,
sidecar-shaped collision data rather than core render geometry. Its four
regions (`vertexPositions`/`faceNormals`/`indices`/`flags`) are each an
independent `(count, offset)` pair read via its own offset, never
accumulated sequentially the way `.phys`'s `PLYT` header+data walk is — the
wiki's own "there can be extra bytes between the data" warning is real,
confirmed on a real file with an 8-byte gap between `faceNormals` and
`indices`. `flagsCount` is also not guaranteed to equal `faceNormCount` (a
real, rare exception — 3/2,354 files) — read independently via its own
header field, never derived from `faceNormCount`. Verified against all
2,354 real `PCOL`-bearing files in the local corpus (corrected from an
earlier scanner bug's false "zero real files," `WIKI_FINDINGS/M2.md`):
every region in-bounds, every index in range for that file's own vertex
count, `indexCount == faceNormCount * 3` on all 2,354 (triangle triples,
one normal per triangle — the same shape M2's own core collision mesh
already has). `flags`' per-record meaning is undocumented on the wiki, but
a full-corpus value scan confirmed it's structurally a real per-triangle
bitmask (every observed value decomposes into a small combinable bit set,
98.4% of files use only bit 0) — individual bit semantics are still
unconfirmed, needing DB2/client data outside this corpus's real M2 bytes
rather than more M2-side investigation (see `WIKI_FINDINGS/M2.md`'s PCOL
section; `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` already tracks real, local
WDC5 DB2 access as planned, staged work for a different feature — the
same access path would apply here too, if `PCOL` bit semantics are ever
worth chasing down).

**`WFV1`/`WFV2`/`DPIV`/`AFRA` (no wowdev.wiki struct at all) are now
structurally parsed by `husk dump-chunks`, not left as a raw hex dump** —
byte-decoded from real corpus files: `AFRA`/`WFV1` are a single fixed
16-byte struct (one real float32 + 12 zero bytes); `DPIV` is a real record
array (`chunk.size / 32` records, 8x float32 each — the wiki's own "always
32 bytes" undersold it, that's just the single-record case); `WFV2` is a
flat 16x float32 array. `WFV1`/`WFV2` are a genuinely thin, 2-file,
byte-identical-content sample, flagged tentative rather than confidently
typed field-by-field (two fields show signs of not really being floats —
see `WIKI_FINDINGS/M2.md`'s follow-up — exposed as plain floats rather
than guessing a color/int reinterpretation). `dumpRawFallback`
(`src/cmd_dump.cpp`) was removed outright once nothing used it anymore.

**`global_flags` decoded into its wiki-named bits, and `textureCombinerCombos`
(the header struct's last field) implemented** — `m2::globalFlagNames`
names every set bit `husk info` already prints as raw hex; a real-file
cross-check confirms `flag_load_phys_data` tracks real `.phys` presence
correctly, and confirms `flag_new_particle_record` is genuinely an
*alternate* signal to `version > 271` (the wiki's own OR), not a second
gate `kMinVerifiedParticleVersion` needs to also check.
`Header::textureCombinerCombos` is read conditionally on the flag bit
(offset 0x130, right after `particleEmitters`) using the same
`parseUint16Array` five other lookup tables already share — a full
130,576-file local-corpus scan found zero real files with the flag set, so
the wiki's own "use this instead of index+1 for multitexture blending"
cross-reference into `cmd_export.cpp`'s material resolution was
deliberately *not* wired up (no indexing key documented at all, and no
real file to verify a guess against) — surfaced via `husk info` only, same
awareness-only treatment `TODO/TODO_correctness.md`'s five-lookup-tables item
already established for this struct.

**`resolveSkin`'s "not found" failure message now names the specific
candidate path it checked**, not just the directory searched — a direct
Foreign Data policy gap (`~/.claude/CLAUDE.md`'s "on failure, always print
expected and actual values"). The sibling resolvers this was checked
against (`--anim`/`--bones-dir`/`--textures`) turned out not to share the
gap: all three are deliberately silent-skip-per-item by design, with no
"not found" failure message to improve in the first place.

**`blp/`'s DXT3 support turned out to already exist — the "unimplemented"
claim was stale documentation, not a missing feature.** A 779,056-file
local-corpus scan (the single longest-running corpus check in this
project's history, ~2h55m, almost entirely disk I/O) found 6,759 real
DXT3 BLP2 files (confirmed-needed, not hypothetical) and zero real JPEG
ones (confirmed genuinely absent, recorded as a real negative result, not
implemented blind). `blp/src/husk_blp/decode.py`'s `_decode_dxt` was
already generic over `PixelFormat.DXT1`/`DXT3`/`DXT5` — DXT3 just had no
test and no real-file verification, so `README.md` kept calling it
unimplemented. Verified both ways before trusting it: a new synthetic
single-block test (matching the existing DXT1/DXT5 precedent), and a real
file (`character/troll/hair00_01.blp`) decoding to a visibly correct hair
texture, not garbage.

**World-scene composition (placing many M2/WMO instances into one rendered
world) will be built bottom-up, never as a monolithic "export the whole
game" operation — confirmed directly by Luna, 2026-08-01.** The actual
CLI subcommand/flag shape for the composing layer is still an open,
deferred design question (see `TODO/WORLD/WORLD_PLACEMENT_TODO.md`'s own
"Scene-composition CLI surface" section), but the *shape of the effort* is
decided: build small, individually testable/exportable primitives first
(a single WMO's own export, a single ADT tile's own terrain export, a
placement-record resolver correct and tested on its own), then add thin
**helper** chaining/composition on top of those once they exist — the same
way husk's own M2 pipeline actually grew historically (`export` started as
"one `.m2` in, one `.glb` out," and `.skel`/`.phys`/`.anim` sidecar
resolution were each pulled in later as independently-testable pieces, not
a single big-bang design). A further, explicitly speculative idea flagged
in the same conversation and *not* decided — whether the real end target
should eventually be a genuine scene-descriptor file format with dynamic
loading (an index/manifest referencing many separately-exported `.glb`s,
loaded on demand, closer to how a game engine actually streams a world)
rather than one large pre-baked `.glb` per tile or map — is recorded as a
potential future exploration goal in `TODO/WORLD/WORLD_PLACEMENT_TODO.md`, not a
requirement blocking any placement-parsing work.

**Anecdotal geoset-group semantics, real Blender inspection of one model
(`bloodelffemale_hd.m2`), cross-referenced against `reference/wow.export`'s
own two independent geoset-group tables — recorded because it's
generalizable groundwork for the geoset-mask work
(`tools/husk_blender_geoset_mask.py`), not itself a husk feature or claim.** husk
doesn't yet parse the real, authoritative per-model geoset-semantics DB2
tables (`CharacterSections`/geoset-group data — locally-extracted DB2
files are in scope per Non-goals above's clarified wording, this is a "not
implemented yet" gap, not a hard non-goal, see
`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`) — what follows is a human visually
identifying what each `group_<n>,variant_<n>` vertex group actually
looks like in Blender on one specific real character export, then checked
against two tables `reference/wow.export` already carries (checked out for
source-code reference only, unrelated to and not to be confused with
Luna's own real local `casc-tool` DB2 export) as a plausibility
cross-check, not verified against real DB2 data. Treat every row as "true
for this one model, not yet confirmed on any other," and the two
reference tables as independently-authored community naming, not
something husk itself reads or trusts at runtime.

`reference/wow.export/src/js/3D/GeosetMapper.js`'s `GEOSET_GROUPS` and
`reference/wow.export/src/js/db/caches/DBItemGeosets.js`'s `CG` enum
(explicitly commented there as "matches `CharGeosets` from WMV," i.e. an
independently-arrived-at community table, not wow.export's own invention)
mostly agree with each other and, strikingly, with every one of this
session's real visual observations except two real naming
discrepancies (both noted below) — this is a much stronger cross-check
than either table alone, since two independently-authored sources and one
independent human visual inspection converging on the same semantics for
14+ of 17 groups is unlikely to be coincidence:

| Group (`geoset_id / 100`) | Real Blender observation, this model | `CG` enum name (`DBItemGeosets.js`) | `GEOSET_GROUPS` name (`GeosetMapper.js`) |
|---|---|---|---|
| 0 (raw IDs < 100) | Hair options | `SKIN_OR_HAIR` | `Hair` |
| 4 | More sleeves and bracers | `GLOVES` | `Gloves` |
| 5 | Boot calf model variations (the actual foot end turned out to be group 20, not this one) | `BOOTS` | `Boots` |
| 7 | Ears | `EARS` | `Ears` |
| 8 | Puffy sleeves (some shirt models, some chest-armor models) | `SLEEVES` | `Wrists` — **disagrees with `CG`**; this model's real content matches `CG.SLEEVES`, not "Wrists" |
| 9 | "Around the knee" pants/shoe mesh variations (flared trouser legs above the knee, chunky boot tops below it) | `KNEEPADS` | `Kneepads` |
| 11 | Leg-armor thigh mesh variations | `PANTS` | `Pants` |
| 12 | Unknown at the time, now identified | `TABARD` | `Tabard` |
| 13 | Skirts(?) — two different meshes that *look* the same | `TROUSERS` | `Trousers` |
| 15 | Cloaks | `CLOAK` | `Cloak` |
| 17 | Eye-glow options (matches this session's own earlier, independently-derived finding — `EYES_ON_FINDINGS.md`) | `EYEGLOW` | `Eyeglow` |
| 18 | Belts | `BELT` | `Belt` |
| 20 | Shoe/boot *foot* variants specifically (the calf portion is group 5) | `FEET` | `Feet` |
| 22 | Something chest-shape-related | `TORSO` | `Torso` |
| 32 | Face — visible by default, only one option on this model | `HEAD_SWAP` | `HeadSwap` — **name mismatch with the visual content on this model**, worth re-checking on a model that actually has 2+ variants in this group before trusting either name over the visual read |
| 35 | Earrings | `PIERCINGS` | `Piercings` |
| 36 | Necklace, independent model | `NECKLACE` | `Necklace` |
| 39 | Arm-band jewelry, independent 3D model | `MISC_ACCESSORY` | `MiscAccessory` |

Groups not directly visually identified this session but present in both
reference tables, listed here so a future session checking a different
group doesn't have to re-derive the base table from scratch: 1/2/3
(`FACE_1`/`FACE_2`/`FACE_3` — facial hair styles, presumably, unconfirmed),
6 (`TAIL`), 10 (`CHEST`), 14 (`DH_LOINCLOTH`), 16 (`FACIAL_JEWELRY`), 19
(`BONE`), 21 (`SKULL`), 23/24 (`HAND_ATTACHMENT`/`HEAD_ATTACHMENT`),
25 (`DH_BLINDFOLDS`), 26-31 (`SHOULDERS`/`HELM`/`ARM_UPPER`/Mechagnome-
specific groups), 33/34 (`EYES`/`EYEBROWS`), 37/38 (`HEADDRESS`/`TAILS`),
40-44 (`MISC_FEATURE`/`NOSES`/`HAIR_DECO_A`/`HORN_DECO`/`BODY_SIZE`), 46
(`DRACTHYR`), 51 (`EYE_GLOW_B` — a second glow channel, also independently
confirmed real by this session's own `EYES_ON_FINDINGS.md` investigation,
matched to real `M2Texture::type == 0` FileDataID-only slots on this same
model).

**A further, real corroboration found in the same reference file**:
`DBItemGeosets.js`'s own `SLOT_GEOSET_MAPPING` (which real equipment slot
controls which geoset groups, transcribed from `WoWItem.cpp` per its own
comment) independently confirms the group-8 naming discrepancy above —
both the Shirt slot (`geosetGroup[0] = CG_SLEEVES`) and the Chest slot
(`geosetGroup[0] = CG_SLEEVES`, alongside `CG_CHEST`/`CG_TROUSERS`/
`CG_TORSO`/`CG_ARM_UPPER`) are documented as controlling `CG.SLEEVES`
specifically — matching this session's own "some shirt models, and some
chest armor models" visual observation almost exactly, and giving no
support at all to `GeosetMapper.js`'s alternate "Wrists" name for the same
numeric group. The Pants/Legs slot similarly controls `CG.PANTS`,
`CG.KNEEPADS`, *and* `CG.TROUSERS` together — consistent with groups 9,
11, and 13 all reading as different real leg-region mesh variations on
this model, per this session's own separate visual findings for each.

**Explicitly not done, flagged for whoever extends this next**: none of
this was checked against a second real model (a different race/gender, an
NPC, or a non-humanoid) — every row above is anecdotal to
`bloodelffemale_hd.m2` alone, and group semantics are plausible but
unconfirmed to generalize (a group's *number* is almost certainly stable
across models per the two independent reference tables above, but which
*specific variant ID* is the "real" default within a group is real,
per-model, DB2-driven customization-choice data husk has no access to, the
same limitation `tools/husk_blender_geoset_mask.py`'s own default-picking
logic already disclaims (`CURATED_DEFAULT_VARIANTS`'s own doc comment)).
No wowdev.wiki page was found with an equivalent table during
this pass — `reference/wow.export`'s two tables above and their shared
`CharGeosets`/`WoWItem.cpp` (WMV) lineage were the only corroborating
source located; if a real wowdev.wiki geoset-group page exists, it wasn't
searched for beyond this repo's own reference material.

**Deliberately unparsed fields — a scope ledger, not scattered inline
claims.** Per `~/nix/claude-rules/CODE_COMMENTS.md`: "out of scope"/"not
implemented"/"stays unread" is a *decision*, not a fact-at-this-line, and
belongs here (reread at onboarding, tolerant of slight staleness), not
buried in a header's doc comment (silently rots — nobody re-reads it on
every edit). This is the reason `TODO/MULTI_TEXTURE_LAYER_TODO.md`'s
`shader_id` investigation found a field husk had "never parsed at all"
despite this project's own completeness docs reading as near-complete: a
real, honest scope cut from early in the project (`skin.hpp`'s own doc
comment, "out of scope for a first metallic-roughness-with-one-texture
pass") never got revisited as the project's ambitions grew well past
"first pass," and nothing forced a re-read of that specific sentence to
notice. First pass at consolidating this (not exhaustive — the rest of
the codebase still has the same pattern scattered in header comments,
flagged as ongoing cleanup in `TODO/CLEANUP_TODO.md`):

- `M2SkinSection` (`skin.hpp`): `centerPosition`/`sortCenterPosition`/
  `sortRadius`/`boneCount`/`boneComboIndex`/`boneInfluences`/
  `centerBoneIndex` are unparsed — culling/skinning-optimization concerns
  for a hardware renderer's own per-drawcall bone limit, not materials;
  husk substitutes full per-vertex global joint indices instead, so
  there's no consumer for these even if parsed (`M2_COMPLETENESS.md`'s
  "Submesh/batch hardware bone-limit metadata" row).
- `M2Batch` (`skin.hpp`): `priorityPlane`/`geosetIndex`/`materialLayer`
  are unparsed, same reasoning as above (draw-order/legacy fields with no
  role in husk's own one-primitive-per-batch model). `shaderId` was the
  one field in this struct that *should* have been revisited earlier and
  wasn't — now parsed and resolved (`src/m2_shader_names.hpp`).
- `.skel` (`skel.hpp`): `SKB1`'s `key_bone_lookup`, and the `SKL1`/`SKA1`/
  `SKPD` chunks entirely, plus `SKS1`'s `global_loops`/`sequence_lookups`
  fields, are unparsed — "extend as later commands need more" policy, no
  concrete consumer identified yet.
- `M2Event` (`m2_scene.hpp`): the `enabled` field (an `M2TrackBase`-only
  timestamp block, "when during playback does this event fire") is
  unparsed — resolving animation-relative timing is a real-clip-playback
  concern a static-scene exporter doesn't have a consumer for; `identifier`/
  `data`/`bone`/`position` (the parts that place the event as a scene
  anchor) are fully parsed and exported.
- `ParticleEmitter` (`m2_scene.hpp`): only the Cata+ `M2Particle` shape
  (492 bytes, gated by `kMinVerifiedParticleVersion`) is parsed. Older
  pre-BC/pre-Wrath/pre-Cata `M2ParticleOld` shapes (narrower
  `blendingType`/`emitterType`, no `multiTexScale`, no FBlock curves, no
  `multiTexScrollMid`/`Range`) are real per wowdev.wiki but not
  implemented — same "verify against a real file before trusting a stride"
  policy `kMinVerifiedRecordStrideVersion` already applies to Bone/
  Sequence/Ribbon (`m2_primitives.hpp`), no real file this project has
  access to exercises the older shape yet.

**A fast reimplementation of husk's own resolution logic must mirror
every tier, not the tiers that seemed obviously relevant.** husk's real
texture resolution (`export_materials.cpp:437-456`) has three ordered
fallback tiers: literal `<FileDataID>.blp/.png`, a `--listfile` real-name
lookup (resolved against the corpus root, not the model's own
directory), then same-basename fuzzy matching. `tools/corpus_scan_tasks/
unfillable_texture_task.py` (a Python reimplementation of this logic for
a fast full-corpus scan, deliberately not shelling out to a real `husk
export` per file — see that file's own docstring) was rewritten away
from an earlier `husk export`-based version that *did* pass `--listfile`,
and the listfile tier was silently dropped in that rewrite, never added
back. Consequence: after a real 18,742-file CASC re-extraction landed
texture files under their real listfile-resolved names (not renamed to
bare FileDataIDs), the scan's flagged-file count didn't move at all —
not because the extraction failed, but because the scan literally never
checked the one resolution tier that extraction was fixing. Caught by
directly tracing one sample file's real DB2/listfile chain by hand
rather than trusting the scan's own unchanged output. The general lesson:
a reimplementation of another system's resolution/fallback logic is only
as correct as its *least* obviously-important tier — cutting one for
"this scan doesn't need to be exhaustive" reasoning silently changes what
the reimplementation actually measures, and the mismatch won't surface
until something upstream changes in a way that specifically exercises
the dropped tier.

**`tools/full_render.py`: full-corpus discovery + a gitignore-style
`.renderignore`, replacing a scan-result-based exclusion list.** An
earlier approach built the render driver's file list by subtracting a
corpus scan's flagged-file CSV from a static `full_corpus_file_list.txt`
snapshot — correct in principle, but fragile in the way the listfile-tier
bug above demonstrates directly: a scan bug (or a stale snapshot) quietly
produces a wrong exclusion list with no obvious symptom until someone
cross-checks it by hand. `tools/full_render.py` instead does a fresh
`rglob` over the real corpus root every run (cheap — ~1-2s for the whole
130k-file tree, confirmed by direct timing, not assumed) and applies a
small, human-editable `.renderignore` (gitignore-style subset: directory-
prefix and glob patterns, `/`-anchoring, no negation/`**`) for exclusion
instead — a category-level decision a human states directly (e.g.
`character/`, structurally DB2-dependent and confirmed as such by direct
investigation) rather than an artifact of whatever a scan happened to
flag on its last run. `render_sample_driver.py`'s own render loop was
factored out into `run_render_pipeline(paths, render_dir)` so both entry
points (the original one-path-per-line CLI, and `full_render.py`'s fresh-
discovery-plus-ignore-file path) share the exact same tested
concurrency/resume/logging logic — never two copies of it.

## CLI argument grammar for `export` (implemented)

**Previous grammar**, for contrast (replaced, not additive — every existing
invocation's argument order changed): `export`'s own `printUsage`
(`src/cmd_export.cpp`) used to take up to three *positional* arguments after
the model (`.skin`|`auto`, output `.glb`, `.skel`), each trailing-optional —
you could stop early, but couldn't skip one in the middle, e.g. giving a
`.skel` meant also giving a skin path and an output path first.
`--textures`/`--skin-dir`/`--anim-dir`/`--lod` were flags layered on top of
that. This grew organically (each positional was added when its feature was
built) and it worked, but it failed `~/docs/CLI.md`'s own §2.2 in one
specific way: a bare word's *meaning* depended on how many other words came
before it, not on what the word itself said — the same failure mode §1
calls out as "no shared grammar." It's also why `--help`/`-h` needed a
dedicated pre-parse check (`commands::isHelpFlag`) instead of Just Working:
a hand-rolled positional parser has no generic notion of "this token is a
flag regardless of position."

**Current grammar**, derived directly from `~/docs/CLI.md` and now real
(`src/commands.hpp`'s `ExportOptions`/`addExportOptions`, `src/cmd_export.cpp`'s
`exportGlb`/`resolveSkin`, both built on
[CLI11](https://github.com/CLIUtils/CLI11)):

| Flag | Short | Meaning |
|---|---|---|
| `--input` | `-i` | the `.m2` path |
| `--output` | `-o` | output `.glb` path |
| `--skin` | `-s` | `.skin` path, or the literal word `auto` |
| `--textures` | `-t` | directory of `<FileDataID>.png` |
| `--anim` | `-a` | directory of `<FileDataID>.anim` (falling back to `<model-basename><animId>-<subId>.anim`), or one of `auto`/`inline`/`none` (see below) |
| `--skin-dir` | *(none)* | directory `auto` searches for the `SFID`-declared `<FileDataID>.skin` |
| `--skel` | *(none)* | external `.skel` path (0-inline-bone models only) |
| `--lod` | *(none)* | `<n>` or `all`, only meaningful with `--skin auto` |
| `--bones-dir` | *(none)* | directory of `<FileDataID>.bone` files, attached as inert extras |
| `--phys` | *(none)* | external `.phys` path, or `none` — attached as inert extras (minimal anchor; full records via `dump-chunks`) |
| `--db2-dir` | *(none)* | directory of real character `.db2` files — texture-layout tables for `--char-layout-id` (`src/chrmodel_db2.hpp`) or `ChrCustomizationElement`/`_Geoset`/`_BoneSet` for `--customization-choice-ids` (`src/chrcustomization_db2.hpp`), same directory serves both |
| `--dbd-dir` | *(none)* | a local WoWDBDefs checkout, resolves `--db2-dir`'s real column names (same role as `husk db2-export`'s own `--dbd-dir`) |
| `--char-layout-id` | *(none)* | a real `CharComponentTextureLayoutsID` (see `husk db2-export`) — husk can't derive this on its own, so it must be given directly |
| `--customization-choice-ids` | *(none)* | comma-separated real `ChrCustomizationChoiceID`(s) (`src/chrcustomization_db2.hpp`) — resolves each to a real geoset selection and/or marks a matching `--bones-dir` correction set as inert extras; husk can't enumerate these on its own either |

Every flag is order-independent. The only positional-shaped things left are
the two every CLI on every platform already trains a user to expect
(`tool input output`, same as `cp`/`mv`) — everything else is named, so
`--help` is trivially always a valid token (no positional-counting to fall
through first) and adding a tenth flag next year can't shift what word #3
means the way it can today.

- **`-i`/`-o` keep a positional fallback, nothing else does** (§1, "habit /
  muscle memory" — the null-then-fallback chain's cheapest rung). `tool in
  out` is load-bearing muscle memory across the entire CLI ecosystem;
  `.skin`/`.skel`/directory arguments have no equivalent universal
  convention to lean on, so spelling them out loses nothing a positional
  would have bought. `--output`'s own default value is a plain computed
  literal (`<model-basename>.glb`) — not an "auto" state in the §2.11 sense
  below, since there's no self-description in the model to defer to for
  "where should this be written."
- **`-s`/`-t`/`-a` get shorthands; `--skin-dir`/`--skel`/`--lod` don't**
  (§2.8, high-frequency flags earn the cheap spelling). `--skin`,
  `--textures`, `--anim` vary on nearly every invocation — that's the
  definition of "common case" §2.8 reserves shorthands for. `--skin-dir`
  doesn't get one *not* because it's rare, but because `-s` is already
  spoken for by `--skin`'s more common meaning (the direct file path/`auto`)
  — a real naming collision, not a frequency judgment (§2.8's actual test:
  "is this a new spelling of an existing flag, or a genuinely different
  axis" — `--skin`/`--skin-dir` are a different axis wearing similar
  clothes, same shape as the `--verbose`/`--debug` example in §2.8, so both
  survive, neither gets collapsed). `--skel` and `--lod` stay unshorthanded
  because they're genuinely occasional — one-shot decisions (§2.6,
  progressive disclosure: common actions at the top level, rare ones one
  step further out), not per-invocation variance.

### Config-file defaults for `export` (implemented, 2026-08-21)

Same flag surface as above, but a subset of these flags (`--dbd-dir`,
`--db2-dir`, `--listfile`, `--listfile-root`, `--textures`, ...) tend to be
per-machine constants — a user's local WoWDBDefs checkout, DB2 extraction
directory, and listfile snapshot don't change between runs, only the model
being exported does. Repeating four or five flags on every invocation is
pure ceremony once that's true.

**Design choice: piggyback entirely on [CLI11](https://github.com/CLIUtils/
CLI11)'s own `App::set_config`, rather than writing a config parser.**
`set_config` maps TOML `key = value` pairs directly onto the same
`add_option` registrations `addExportOptions` already declares — so a
config value gets the exact same `->check()` validator a CLI flag value
would (foreign-data checking "for free," §CLAUDE.md's Foreign Data policy —
a config-supplied `--skin none` is rejected exactly like a hand-typed one,
with zero new validation code), and CLI11's own precedence order already
matches what was wanted: an explicit command-line flag beats a config
value, which beats the flag's own built-in default. No second parser, no
hand-written key→flag table to keep in sync with `addExportOptions` as
flags are added or renamed.

**Path resolution**: `--config <path>` > `$HUSK_CONFIG` (via `->envname`)
> `husk::defaultConfigPath()` (`src/husk_config.hpp`) — `$XDG_CONFIG_HOME/
husk/config.toml`, falling back to `~/.config/husk/config.toml`. A missing
file at the resolved path is not an error (`set_config`'s own
`config_required=false`) — same "unset is the no-flag state" convention
every other opt-in sidecar in this project already follows. The path
helper is a standalone shared function, not export-specific, so the other
commands `TODO/CLEANUP_TODO.md` #3 tracks migrating to CLI11 can reuse it
verbatim rather than re-deriving the same XDG logic.

**Deliberately not filtered to a "safe" subset of flags.** CLI11's config
support has no notion of "these flags are config-settable, those aren't" —
every flag `addExportOptions` registers, `--input`/`--output` included, is
technically settable via config. Building a filter to block that would be
new code whose only job is fighting the very mechanism chosen to avoid new
code. Instead: the documented example config (`README.md`) only shows the
genuinely per-machine flags, so a user finds `output = "..."` only by
reading the flag table or the source, never by accident — and if someone
deliberately sets it anyway, they did so in the same validated TOML syntax
this project already accepts everywhere else, which is a legitimate (if
unadvertised) choice, not a bug to guard against.

### Three-state resolution, not two (§2.11)

`--skin`, `--textures`, `--skin-dir`, `--skel`, and `--bones-dir` all name a
thing the model's own chunk data (or its presence next to the model) already
points at — `~/docs/CLI.md` §2.11 states the rule for exactly this shape of
flag directly: *"there are three states here, not two"* — collapsing "leave
it unset" and "actively don't want it" loses the difference between "try to
resolve, warn if broken" and "I already know, don't even try."

- **Unset -> `auto`.** Best-effort derivation from whatever the input
  already declares — and this is deliberately **not** defined as "check the
  model's own directory." That directory is *one current heuristic* for
  *where to search*, not what `auto` means. The two are genuinely separate
  questions with separate answers:
  - *What* to look for is derived from the model's own bytes wherever the
    model states it explicitly — `--skin auto` reads the `SFID` chunk (entry
    `--lod` selects) for the FileDataID to search for; that answer never
    depends on the filesystem at all.
  - *Where* to search is a directory, and today that directory's own
    default happens to be the model's own directory (a real extraction
    drops everything into one place) — but that's this flag's default
    *value*, not a redefinition of `auto` itself. A future second signal
    for "where" (a manifest file, a second sidecar) would extend the
    heuristic without changing what any of these flags mean to the user.
  Leaving the definition of "auto" at "derive from self-description,
  best-effort" rather than baking in today's one heuristic keeps the
  contract stable if the heuristic ever grows a second source.
- **Explicit value -> override.** Use exactly this file/directory instead
  of deriving anything.
- **Explicit `none` -> skip deliberately.** Distinct from unset: the user
  already knows this reference is missing or broken and wants the rest of
  the export to proceed without a warning or a resolution attempt — the
  strongest form of "don't even try."

What `none` means, concretely, per flag:

- `--textures none` — never embed an image, even if a matching
  `<FileDataID>.png` would otherwise resolve. Materials still get the
  correct blend mode/tint either way.
- `--skel none` — never look for a same-basename `.skel` next to the model,
  even if one exists there. Forces an unskinned mesh regardless of the
  model's own inline-bone count.
- `--skin-dir none` — `--skin auto` skips the `SFID`-FileDataID search stage
  entirely and falls straight to the same-basename numbered scan.
- `--bones-dir none` — never resolve any `.bone` file, even if a matching
  `<FileDataID>.bone` sits in the default directory; no `bone_correction_sets`
  extras attached at all.

**`--db2-dir`/`--dbd-dir` are required together; `--char-layout-id` no
longer needs its own explicit value** (superseded, 2026-08-21: this used
to be "all three given, or the feature is off," since husk had no way to
derive a `CharComponentTextureLayoutsID` from an `.m2` on its own — but
`--chr-model-id auto` already derives a real `ChrModelID` from the same
`ChrModel.db2` table that carries `CharComponentTextureLayoutID` on the
same row, so `attachCharTextureLayout` (`cmd_export.cpp`) now auto-derives
the layout ID from whichever `ChrModelID` `--chr-model-id` resolves — same
`auto`/`none`/`<id>` three-state control as `--chr-model-id` itself,
found and closed same-day after a real interactive-use question: "are
`--db2-dir`/`--dbd-dir` strictly required, or nice-to-have?"). Given
`--db2-dir` without `--dbd-dir`, `attachCharTextureLayout` still prints a
diagnostic and skips, same as before — never a hard failure of the rest
of the export.

**`--anim` needs four states, not three — it bundles two independent
questions the generic pattern above collapses into one.** "Should any
animation data exist at all" and "should *external* `.anim` files
contribute to it" are separate axes: inline sequences (`flags & 0x20`) and
global-sequence bone tracks are both resolved straight from the model's own
blob, with no filesystem involved, so "skip external resolution" and "skip
animation entirely" are genuinely different outcomes. Collapsing them into
one `none` (as first drafted, then corrected) silently picked one meaning
and lost the other. Four states:

- `--anim auto` (**default**) — today's combined behavior: inline
  sequences and global-sequence tracks always resolved from the model's own
  blob, *plus* best-effort external-directory search (default: the model's
  own directory) for sequences not stored inline.
- `--anim inline` — inline sequences and global-sequence tracks only;
  external-directory search is explicitly skipped. This is what a bare
  `none` was first written to mean above, and why it needed its own name
  instead of overloading `none`.
- `--anim none` — no animation data at all, full stop: a static, still
  possibly-skinned (bind-pose) mesh with zero glTF `animation` clips,
  inline or external. The bind pose itself (`JOINTS_0`/`WEIGHTS_0`, inverse
  bind matrices) is unaffected — this flag only ever touches `animation`
  clips, never the skin itself.
- `--anim <dir>` — explicit directory override for the external-search
  stage; inline sequences and global-sequence tracks are still resolved on
  top of it, same as `auto`.

### Does `inline` generalize past `--anim`?

Worth deriving as a general rule rather than deciding per-flag by feel,
since this tool's whole premise is comprehensiveness — a rule that's checked
once against every current flag is worth more than four separate judgment
calls that might silently disagree later.

**A flag earns a fourth `inline` state, distinct from both `auto` and
`none`, only when both hold:**

1. The model's own blob can independently produce a *non-empty, meaningful*
   result for that data category with zero filesystem access, **and**
2. that inline-sourced result and an externally-sourced result are not
   mutually exclusive — both can be true at once, and the export *combines*
   them rather than picking one.

`--anim` is the only current flag meeting both: inline `M2Sequence`/
global-sequence bone tracks are self-sufficient (condition 1) and additive
alongside `.anim`-sourced sequences — a real export can and does carry both
kinds of clip side by side (condition 2). Checking the other candidates
against the same rule, not just asserting "no":

- **`--skel` fails condition 2.** Inline `bones` and an external `.skel`
  are mutually exclusive in every real file this project has examined, and
  in the pipeline's own resolution order (`DESIGN.md`'s Pipeline section,
  step 4: "if inline `bones` is empty *and* an `SKID`/`.skel` path is
  available") — an `SKID` chunk only shows up when inline bones are already
  empty. A `--skel inline` state would be indistinguishable from `auto`
  whenever inline bones exist (both just use them), and indistinguishable
  from `none` whenever they don't (nothing to use either way) — a state
  that can never independently change behavior is worse than no state:
  it's one more thing to document and test that measures nothing. If a real
  file ever surfaces genuine inline+external combination, that's new
  evidence worth reopening this against — same "verify against real data
  before trusting a claim" bar this project holds everywhere else — but
  nothing observed so far supports it.
- **`--textures` fails condition 1.** WoW never embeds pixel data in the
  M2/`.skin` itself — every texture, including the `type == 0` "real
  filename" case, is BLP-external by construction (`src/m2.hpp`'s `Texture`
  doc comment). There is no inline image to fall back to, so `inline` here
  would just be `none` under a different name.
- **`--lod` isn't this shape of flag at all.** It's an index selector into
  already-resolved `SFID` entries (which tier, or `all`), not a
  resolution-source flag with an inline/external axis to begin with — the
  auto/`none`/`inline` framing doesn't apply to a selector.
- **Other glTF `extras` this tool bakes in** (geoset ID/group/variant,
  second-texture-layer metadata, texture-transform data, ribbon/particle
  placement anchors) **aren't gated by any resolution flag at all**, inline
  or otherwise — they're read unconditionally from the model's own
  `.skin`/`M2` data every export, with no external counterpart to combine
  with or skip. Nothing to extend here because there's no flag in the first
  place.

**`--skin` doesn't cleanly extend to the three-state table above either,
and that's worth stating outright rather than fudging.** The other two
flags there (`--textures`, `--skel`) gate *optional
enrichment* — at `none`, the export still succeeds (an unskinned mesh, a
flat-tinted material, fewer clips). A `.skin` file is not optional
enrichment: it's the *sole* source of triangle winding, submesh, and batch
data (`skin::resolveTriangleIndices`/`parseSubmeshes`/`parseBatches`,
`src/skin.hpp`) — there is currently no code path that emits even one glTF
primitive without one.

**Decided: `--skin none` is rejected outright**, not accepted as a real
state. A `.skin` is load-bearing, not optional enrichment, so `none` isn't
meaningful for it — CLI11 validates this at parse time with a clear error
naming the actual expectation (`--skin` takes a path or `auto`, never
`none`). A genuine skin-less export (a raw, un-triangulated point cloud
straight from the M2's own vertex array, no submesh/material structure at
all) would be real new scope this file has never tracked before, and isn't
being added as a side effect of a CLI-grammar change — if that's ever
wanted, it gets its own design discussion, not a quiet `none` case here.

**Library: [CLI11](https://github.com/CLIUtils/CLI11)**, header-only, MIT,
chosen over hand-rolling a second generation of the current parser or adding
a Boost-style everything-framework — matches the project's existing
tight-fit-library-only policy (`tinygltf` for glTF framing, `tinyobj`-style
single-purpose deps elsewhere). Declaring `-i,--input`/`-o,--output`/etc.
with a description string at the declaration site gets `--help` generation,
order-independent parsing, and the positional-fallback behavior above for
free, instead of hand-maintaining all three the way `printUsage` does today.

**Rejected alternative: wrap the CLI in a Python layer.** husk-the-binary
stays pure C++ — this would cut against the already-stated `.blp` process
boundary above (husk never invokes another process or links a scripting
runtime into its own control flow); a Python wrapper for argv handling
blurs that same line from the opposite direction for no benefit CLI11
doesn't already provide directly in C++.

**Impact, landed**: `src/cmd_export.cpp`'s old positional-parsing block and
`printUsage` are gone, replaced by `addExportOptions`/`exportGlb` above;
`README.md`'s `export` Usage subsection (defaults/flags table/examples) and
`tests/test_cli.cpp`'s positional-ordering cases were rewritten together —
this was a breaking change to every existing `husk export` invocation's
argument order, not an additive one. `nix/flake.nix`/`CMakeLists.txt` gained
CLI11 as a new dependency (`pkgs.cli11`, `find_package(CLI11 CONFIG
REQUIRED)`, linked `PUBLIC` on `husk-lib` so `main.cpp`'s completion
generator gets it transitively too), signed off before landing per this
project's package-approval rule. Not purely mechanical: what used to be "no
`.skin` given at all" (`findSameBasenameSkins`'s scan) and "`.skin` given as
`auto`" (`SFID` + `--skin-dir`) as two independent code paths folded into
`resolveSkin`, the single function behind the one `--skin` flag (default
`auto`) — it tries both conventions in order, inside whatever `--skin-dir`
resolves to (itself defaulting to the model's directory, so this is
normally one search in one folder, not two): **`SFID`-declared FileDataID
match first** (the model's own self-description, §2.11 — decided over
trying the basename scan first), **same-basename numbered scan second** as
the fallback, only attempted once the SFID stage is confirmed unavailable
(no chunk, `--skin-dir none`, or the declared file doesn't actually exist).
`--skin none` itself is rejected at parse time by a CLI11 `Option::check`
validator on `--skin` (decided above), so `resolveSkin` never needs to
handle a "deliberately skip" case. Separately, the old single boolean-ish
"is `--anim-dir` given" check was replaced with real state handling for
`--anim`'s four values (`auto`/`inline`/`none`/an explicit directory) in
`exportGlb`: `none` sets a dedicated `animNone` flag gating the entire
per-sequence-plus-global-sequence animation-building call (both were
previously unconditional whenever bones existed); `inline` leaves the
resolved `animDir` empty, which `buildAnimations`' own external-sequence
branch already treated as "skip external resolution" before this change —
no new parameter needed there, just a new way to reach that existing state
deliberately instead of only via a missing `--anim-dir`. One real
implementation subtlety worth flagging for future readers: CLI11's
`App::parse(std::vector<std::string>&)` overload consumes tokens from the
*back* of the vector (mirroring how its `(argc, argv)` sibling reverses
argv internally before the same call) — `exportGlb` reverses its `args`
array into that order before calling `app.parse(...)`; passing it in
forward order silently binds every flag to the wrong neighboring token
instead of erroring, which is exactly what happened during this migration
before the reversal was added, so don't drop it in a future refactor.

### Shell completion generation

`completions/husk.bash`/`.zsh` (checked in) are captured output, not
hand-written — `src/main.cpp`'s hidden `--print-completion=<bash|zsh>` flag
builds a throwaway `CLI::App` tree (`export` via the real
`addExportOptions`, so its flag surface can never drift from what
`exportGlb` actually parses; `info`/`dump-chunks` hand-registered, since
neither is CLI11-based — both take exactly one positional argument and
nothing else) and walks it with CLI11's own introspection API
(`get_options()`, the filtered `get_subcommands()`, `Option::get_lnames()`/
`get_snames()`/`get_expected_max()`), never `.parse(...)`. This exists
because no C++ argument-parsing library (CLI11 included — checked its own
docs, not assumed) generates shell completions, unlike Rust's
`clap_complete`, Go's Cobra, or Python's Click/Typer; generating from the
live option objects rather than hand-listing flags a second time is what
keeps the completion scripts from silently drifting the next time a flag
is added or renamed. `--print-completion` itself is deliberately absent
from `husk --help` (it has no human reader — its only consumers are this
capture step and the installed completion script's own callback) rather
than hidden via CLI11's `->group("")` mechanism, since `husk`'s top-level
dispatch isn't itself CLI11-driven (see the previous-grammar note above:
`info`/`dump-chunks` stay hand-rolled by design, so there's no top-level
`App` whose help output needs suppressing in the first place).

## Boundaries (where foreign data enters)

- Model file bytes (`.m2`) — chunk container + fixed-offset header/arrays.
- `.skin` sidecar — triangle-index lookup, submesh/batch structure.
- `.skel` sidecar — external bones + sequences.
- `.bone` sidecar — per-bone correction matrices (reverse-engineered),
  surfaced as inert glTF `extras` via `--bones-dir`, never applied to the
  render (see Key design decisions).
- `.anim` sidecar — external per-sequence keyframe blob, `AFM2` (flat) and
  `AFSB` (`.skel`-linked models' real shape) both resolved.
- `.phys` sidecar — physics/collision bodies/shapes/joints, documented on
  wowdev.wiki (not reverse-engineered), byte-reversed chunk tags (WMO/ADT
  convention). Minimal placement anchors surfaced as inert glTF `extras` via
  `--phys`, full records via `dump-chunks`, never applied to the render (see
  Key design decisions).
- `--textures`/`--skin-dir`/`--anim`/`--bones-dir` directories —
  user-populated, FileDataID-named, local filesystem only. Never CASC.
  `--phys` is a same-basename-*file* convention instead (mirroring
  `--skel`), not a directory.
- `.blp` texture files (separate `blp/` Python tool) — container + mip
  table hand-rolled, block decode delegated to Pillow via synthetic DDS.

Every boundary above is read via explicit bounds-checked parsing with
named offsets, and every ambiguous byte-layout question is resolved against
real decoded file data before being trusted (see `WIKI_FINDINGS.md` for the
receipts) — never guessed from prose alone when a real file was available
to check against.

## Testing architecture

Four tiers, the first three the same shape used by `casc-tool`:

1. **Pure-logic** (`test_chunk`/`test_m2`/`test_skin`/`test_skel`/
   `test_gltf`) — synthetic buffers built field-by-field from the wiki
   spec (or, for `gltf`, round-tripped through tinygltf's own loader), no
   real files, always run.
2. **Command-layer** (`test_cli`/`test_dump`) — spawns the real compiled
   binary against small synthetic fixtures; exercises argv parsing and
   `cmd_*.cpp` exception handling without needing real game files.
3. **Integration** (`test_integration`) — the compiled binary against real,
   game-extracted files. Asserts on shape ("found some vertices," "plausible
   `.glb`"), never on one specific model's exact field values — those
   belong in tier 1. Each real-file fixture resolves via
   `tests/test_data_paths.hpp`: an explicit `HUSK_TEST_*` env var always
   overrides; otherwise it falls back to a matching file already in this
   repo's own gitignored `test_data/` (two fixtures are *constructed* on
   the fly rather than resolved directly: `--skin-dir`'s, by reading the
   real SFID entry 0 out of the resolved M2's header; `--bones-dir`'s, by
   reading the real `.skel`'s own `BFID` chunk and copying a few of this
   repo's already-present `.bone` fixtures under those FileDataIDs — the
   same shape a hand-populated directory would need either way). A fixture that doesn't resolve
   marks its `TEST_CASE` with `* doctest::skip(...)`, not a runtime
   `MESSAGE` + early `return` — doctest's own summary then reports a
   distinct, non-zero "skipped" count instead of silently folding a
   0-assertion test into "passed" (a real gap this project's own audit
   found: `./build/husk-tests` used to report "0 skipped" even when 12 of
   260 cases never exercised anything). `tests/test_main.cpp`'s startup
   banner prints every fixture's resolution (or lack of one) up front, so
   "why did N tests just skip" is a read, not a rerun with env vars
   guessed at.

4. **Conformance** (`test_conformance`) — real downstream *consumers* of an
   exported `.glb` (Khronos `gltf_validator`, Blender's own importer run
   headlessly), plus a third comparison leg the other tiers don't reach:
   the M2 source file's own header counts, cross-checked against what
   Blender/tinygltf actually read back (vertex/bone counts exactly,
   bind-pose bounds via containment, collision-mesh count/topology
   exactly — see `WIKI_FINDINGS/M2.md` for the bounding-box finding. See
   `README.md`'s Testing section for the full per-check writeup, including
   two real Blender-importer-side contamination sources found while making
   these checks exact.

Known gap: the ad hoc real-`.anim`-directory verification described in the
README's roadmap stage 6 (50/50 and 54/54 real files, parsed back apart in
Python) has no `HUSK_TEST_ANIM_DIR`-gated repeatable test yet — it doesn't
re-run automatically. Tracked as a testing debt, not a correctness bug.

**A second, more fundamental known gap, named honestly by Luna 2026-08-01**:
every Blender-side check this tier ever runs is against **headless**
Blender (`tests/blender_import_check.py`, invoked with no display) — count/
topology/validator-style assertions, never an actual human looking at the
rendered result in Blender's own GUI. This has been true since
`test_conformance` was introduced and applies to every feature husk has
ever shipped (collision meshes, ribbons/particles, multi-root skeletons,
`.bone` corrections, `.phys` bodies, geoset/texture-transform `extras`),
not just anything new — no session so far has had real eyes on whether any
of this actually *looks* right once imported, only whether it's
structurally present and countable. Headless checks are real and valuable
(they catch the contamination/regression bugs `WIKI_FINDINGS/M2.md` and
earlier sessions found) but they cannot catch a visually-wrong-but-
structurally-valid export — wrong-looking UVs, a flipped normal, a
correctly-counted but misplaced vertex. Worth a real interactive-Blender
pass at some point on a representative fixture or two, entirely separate
from (and not a replacement for) the automated headless tier — not done as
part of any session so far, tracked here so it isn't silently forgotten.

## Open work

See `TODO/TODO_correctness.md` for the current punch list (`M2Camera`, `.bone`
slot selection, and two awareness-only footnotes) and `WIKI_FINDINGS.md`
for every real-data-driven spec correction found so far, `AFSB`'s
included. `TODO/TODO_correctness.md`/`WIKI_FINDINGS.md` are living documents;
this file describes the shape of the system they operate within, not
their current item-by-item status.

`M2_GAPS_TODO.md` (documented-but-unbuilt M2 coverage items with no
external-data blocker: `M2Sequence`'s remaining fields including
`aliasNext` chain-following, `PFDC`, `EXP2`, `Texture.type`,
Attachments/Events/Lights, animated tint/fade, `DETL`, `PCOL`, plus
regression-test follow-ups for `EXP2`/`PFDC`/a `BLP2`-anomaly fixture) is
now fully implemented and removed once every item had a final
disposition -- same "survey's job is done" lifecycle every prior TODO
file in this project has used. See Key design decisions below and
`M2_COMPLETENESS.md` for the permanent per-feature record.

A dedicated investigation brief for the last genuinely undocumented M2
pieces (`M2_UNKNOWNS_EXPLORATION.md` -- wowdev.wiki itself had no
field-level struct, or an internally-inconsistent one, for `WFV1`/`WFV2`/
`DPIV`/`AFRA`/`DETL`, plus the `M2Sequence.aliasNext` resolution
mechanism) has since run to completion and was removed once every target
had a final disposition: `WFV1`/`WFV2`/`DPIV`/`AFRA` were originally reported
confirmed-absent from a full real 130,576-file corpus sweep, but that was a
scanner bug (a `bytes`-vs-`str` dict-key mismatch that made the check always
false), not a real result -- corrected: all four (plus `PCOL`) are real and
present, cross-checked independently via `casc-tool scan-chunks`
(`WIKI_FINDINGS/M2.md`), and now all five are fully implemented
(`husk dump-chunks`'s `dumpPcol`/`dumpWfv1`/`dumpWfv2`/`dumpDpiv`/`dumpAfra`
-- see Key design decisions below);
`DETL`'s real byte layout is fully resolved (12-byte stride, zero-padded to
a 16-byte boundary -- §11); `aliasNext` is a local `sequences`-array index,
not an external id, at the `M2Bounds`-corrected offset 0x3E (§12), now
implemented end to end (see Key design decisions below).

`.phys` physics/collision sidecar support used to have its own living plan
here too (`PHYS_TODO.md`) -- now fully implemented (`src/phys.hpp`/
`phys.cpp`, `husk export --phys`, `husk dump-chunks <file.phys>`) and folded
into this file's own Key design decisions (the `.phys`-anchor/dump-chunks-
split entry above) and `WIKI_FINDINGS/PHYS.md`'s "Where these live in husk"
row, so the standalone file was removed. Its own coverage table (verified
vs. unverified per chunk type, `WIKI_FINDINGS/PHYS.md`) is unaffected — real
corpus never surfaced `BDY2`/`BOXS`/`WLJ3`/`SHOJ` (0x6c)/`SHJ2`/`REV2`/
`SPHJ`/`PRSJ`/`PRS2`/`DSTJ`, so those chunk types are parsed (the byte
offsets are real, transcribed from the wiki) but structurally unverified
against a real file — `src/phys.hpp`'s own doc comment carries this forward
now, same "verified floor, awareness only" treatment
`kMinVerifiedRecordStrideVersion`/`kMinVerifiedParticleVersion` already
established for other version-gated shapes.

The multi-root-bone-forest representation gap used to have its own living
plan here too (`MULTIROOT_SKELETON_TODO.md`) -- now fully implemented and
folded into this file's own Key design decisions (the
synthesized-non-joint-parent-node entry above) and `src/gltf.hpp`'s
`Skeleton`/`writeGlbMulti` doc comments, so the standalone file was
removed. Two narrow questions from that investigation were never chased
down and remain genuinely open -- low-priority, awareness-only, not
blocking anything built:

- **What `gltf_validator`'s `SKIN_NO_COMMON_ROOT` check actually measures.**
  Empirically it flags only ~7% of a random 150-file multi-root sample, and
  neither raw root count nor vertex-weighted root count predicts which
  files trigger it (`bloodelffemale.m2` has 90 of 119 roots and zero
  errors; `offhand_1h_revendreth_d_01.m2` has only 10 of 15 and does
  error) -- its real trigger condition was never reverse-engineered.
- **Why an 11-hit sample from that 150-file draw skewed toward one item
  family** (9 of 11 were `helm_mail_zuldazarraidmythic_d_01_*` race/gender
  variants of the same base item) -- never investigated; could be
  bone-count- or hierarchy-shape-correlated, or just sample-size noise.

Separately, `M2CompBone.flags & 0x200` ("transformed" per the wiki)
correlates with 66 of 78 roots in the flattest real multi-root fixture
found (`mace_2h_bolvar_d_01.m2`, every one of its 78 bones its own root),
but what actually distinguishes the flagged from unflagged roots was never
determined -- worth a real investigation only if a future design ever
wants to treat root bones differently based on it, per this project's own
don't-guess-at-semantics rule. None of these three are blockers; they're
recorded here so a future session doesn't have to rediscover them.

`TOOL_COMPARISON.md` (new) is a static source-level comparison against
wow.export (Kruithne/wow.export, cloned into gitignored `reference/
wow.export/`), the real community-standard tool for the same M2→glTF
problem — requested directly, scoped to source-reading only (no live
export diff; wow.export is unstable enough on Luna's own account that a
real side-by-side run is deferred as future work, tracked in that file's
own closing section). Headline finding: husk and wow.export solve
adjacent but different problems — wow.export has live CASC+DB2 access
(real character-customization-driven texture/geoset resolution, exactly
the external data source `TODO/TODO_correctness.md`'s `.bone`-slot-selection
gap and this file's `Texture.type` handling both point at -- husk's own
path to the same data is locally-extracted DB2 files, not live CASC, per
Non-goals above's clarified wording) and broader
format scope (WMO/ADT/M3), but its own M2 loader has zero code path at
all for ribbons, particles, events, lights, cameras, or the `.phys`
sidecar — confirmed by reading its source directly (dead
`// this.data.move(8)` skip-comments, and no `PHYSLoader.js` file exists),
not inferred. Also surfaced 3 real corpus files with a genuinely new,
not-yet-tracked animation-data failure shape (NaN keyframes / a ~2.2
billion ms backward timestamp jump) — see that file's own closing
section for the exact files.

`WORLD_COMPLETENESS.md` (WMO/ADT/WDT/WDL/PM4/PD4, previously a target-
setting scaffold with every row reading `none`/`none` and "documented, not
verified") went through one full real-data investigation pass and was
expanded into eleven implementation-ready companion documents: `TODO/WORLD/WDT_TODO.md`,
`TODO/WORLD/ADT_TERRAIN_TODO.md`, `TODO/WORLD/ADT_LOD_TODO.md`, `TODO/WORLD/WMO_GEOMETRY_TODO.md`,
`TODO/WORLD/WORLD_PLACEMENT_TODO.md`, `TODO/WORLD/LIQUID_TODO.md`, `TODO/WORLD/LIGHTING_TODO.md`,
`TODO/WORLD/FOG_VOLUMES_TODO.md`, `TODO/WORLD/COLLISION_CULLING_TODO.md`,
`TODO/WORLD/WORLD_MISC_METADATA_TODO.md`, `TODO/WORLD/PM4_PD4_TODO.md` — same "survey first,
implementation-ready plan before code" shape `PHYS_TODO.md`/`ANIM_TODO.md`
used for their own formats, just at the scale of an entire new format
family rather than one sidecar. Nothing in `src/` reads a WMO/ADT/WDT/WDL/
PM4/PD4 byte yet; `WORLD_COMPLETENESS.md` itself still reads `none`/`none`
everywhere. Every real correction found (chunk-tag byte reversal confirmed
universal across all four containers, several wiki struct errors fixed,
four gameplay/misc-metadata items promoted out of a reflexive `n/a`, two
genuinely undocumented chunks found, the portal-culling/Blender-visibility
question checked directly rather than assumed) is recorded permanently in
`WIKI_FINDINGS/WORLD.md` — see that entry for the consolidated list, and each
`*_TODO.md` file for the full per-item struct, C++ data-model sketch, and
test plan. `WORLD_COMPLETENESS.md`'s own "Recommended implementation
order" note (added this pass) is the starting point for whoever picks this
up: `.wdt` first (nothing else can locate a real file without it), then
ADT terrain, then world placement (`MDDF`/`MODF` — "the single most
important row" in that whole file), then the rest in roughly
prevalence-and-value order.

PM4/PD4's own non-goal framing above is corrected by this same pass —
`LUNA_NOTES.md` pointed out directly that "never touched by the client
renderer" doesn't mean "excluded from a world-completeness file," the same
way M2's own collision mesh is real, exportable geometry despite never
being drawn by a normal render pass. `TODO/WORLD/PM4_PD4_TODO.md` found a genuine
structural wall worth flagging here directly: these files are **never
shipped to the client at all** (confirmed via a full `casc-tool list`
sweep of all 3,190,909 files in live retail storage — zero `.pm4`/`.pd4`
matches anywhere), not an extraction-completeness gap like `EXP2`/`PFDC`
(`WIKI_FINDINGS/M2.md`) — real fixture data for this one format will need
a different acquisition path than everything else in this project's usual
corpus-verification playbook.

**A companion Blender-side script that hides `extras`-tagged-but-visible
geometry (collision meshes, PM4/PD4 pathing meshes, and any future item
that lands on this same "present, identifiable, not literally hidden"
shape) post-import is real, deliberate usability tooling for later, not
part of any export-path decision made so far** — noted directly by Luna,
2026-08-01, alongside the PM4/PD4 visibility decision above. Every one of
husk's "tagged, not hidden" items (the M2 collision mesh, and now PM4/PD4
pathing meshes once implemented) would benefit from exactly this, but it's
explicitly deferred until the tool is at a point where someone actually
wants to *use* exports interactively, not built speculatively now — same
"don't build ahead of a real need" discipline this project applies
elsewhere (`CLAUDE.md`'s "Abstractions are earned").

That "real interactive use" moment arrived the same day: Luna's own first
hands-on Blender pass over real M2 export output (not headless-Blender
structural checks, an actual human looking at the result) found several
real things purely-structural verification can't catch — written up as
`BLENDER_EXPORT_TODO.md` (new), then, per her own explicit "implement
what doesn't need my input, have fun tinkering" while she slept, worked
through the same night. Final state of that file:

- **The headline finding, and the reason anyone opening this repo next
  should read `BLENDER_EXPORT_TODO.md` §8 before anything else**: husk's
  M2→glTF position export is measurably **upside down** — confirmed via a
  real headless-Blender import of a real export (not eyeballed): a
  head-height landmark bone lands *below* the root/feet-level bone in
  Blender's own imported world space, and the render mesh's bounding box
  sits entirely on the wrong side of the origin. `src/gltf.cpp`'s
  `zUpToYUp` matches its own cited wowdev.wiki source formula exactly —
  this isn't a transcription bug — but composing it with Blender's own
  glTF-import axis conversion doesn't net out to identity the way
  round-tripping the same physical up-axis should. A one-line sign flip
  was tested and empirically confirmed to fix it (476/477 tests still
  pass — only the `zUpToYUp` unit test itself, which hardcodes the current
  formula, fails) — **then deliberately reverted, not shipped**: `DESIGN.md`'s
  own rotation/scale quaternion conversion (two paragraphs below) was
  explicitly derived *from* this exact position permutation, so a real fix
  needs those re-derived and re-verified too, not just the position half,
  and this touches every position/rotation/scale husk has ever exported —
  a whole-tool-blast-radius change that needs Luna's own review, not a
  same-night autonomous fix. Full receipts and the exact math were in
  `BLENDER_EXPORT_TODO.md` §8 (since resolved for real — see the
  `kWowToGltf` entry earlier in this section — and, with every other item
  in that file also resolved, the file itself deleted; the fix's own
  history lives on in this entry, `TRANSFORM_TRIAGE.md`, and
  `CLAUDE_HISTORY.md`).
- The original leading hypothesis for two other symptoms ("boot vertex
  parented to the other leg's bone," the base body mesh appearing to go
  missing) — that `.skin`'s own "Bones" lookup array and the M2 header's
  `boneCombos` table are parsed but never dereferenced, so vertex joint
  indices might need an indirection husk skips — was investigated and
  **closed as not a bug**: an independent real-data cross-check found
  husk's raw `M2Vertex.bone_indices` read already matches the
  wiki-disputed indirection formula's own result, 9,143/9,143 real
  weighted bone slots, no divergence. The missing-body symptom's likely
  explanation is now the orientation bug above instead, unconfirmed until
  someone looks again after §8 has a real fix.
- Four smaller items shipped, tested, and verified against real exports
  this same night: `husk export --collision none` (debuggability — the
  collision mesh renders like any other mesh in Blender's stock importer,
  with no way to opt out before this); real material names
  (`m2::textureTypeName`, e.g. `_skin`/`_char_hair`, replacing a bare
  `_tex2`) plus a real filename-matching fallback for texture slots husk
  has no FileDataID for (Luna's own idea, mid-session: real texture
  directories are sometimes named descriptively rather than by
  FileDataID — a same-session self-caught bug in the first draft, where a
  single real candidate file got wired into *every* unresolved slot at
  once instead of just one, is also recorded there); and real bone/joint
  node names (`m2::keyBoneName`, wowdev.wiki's own 193-row key-bone table)
  instead of Blender's generic "Bone"/"Node" numbering.
- Bone tail directions all pointing the same default direction regardless
  of hierarchy shape was investigated headlessly and is very likely a
  genuine Blender-importer limitation (glTF carries no bone-length data at
  all) rather than a husk bug — not being pursued further.

**Follow-up, next session: the upside-down bug above is fixed, tested, and
shipped** — not just the reverted one-line sign flip §8 above describes.
Requested directly, after the finding above: not a quick patch, but "a more
robust system that can test the correctness of the mesh regardless of the
rotation... research and explore how to fix this permanently" — written up
as `TRANSFORM_TRIAGE.md` (a full root-cause/process-failure/durable-fix
investigation), then, once Luna reviewed and answered its open questions
directly ("yes, you build while i nap"), implemented the same session.
- **`src/gltf.cpp`'s `zUpToYUp` is now one leg of a genuinely single source
  of truth**: a private `Mat3` plus one matrix (`kWowToGltf`), with
  `zUpToYUp`/`rotationZUpToYUp`/`scaleZUpToYUp` all mechanically derived
  from it (position/normal: direct matrix application; rotation: quaternion
  → matrix → conjugate by the matrix → quaternion; scale: the matrix's
  permutation with signs dropped) — not three independently hand-typed
  formulas that could silently drift out of sync with each other, which is
  exactly the shape of bug this whole investigation traced back to (the
  rotation/scale conversions in `cmd_export.cpp` used to be hand-derived
  *from* the position formula's own math, on paper, then never
  independently re-verified). `cmd_export.cpp`'s `toGltf(m2::Quat)`/
  `toGltfScale` are now thin wrappers. A `static_assert` on the matrix's
  determinant enforces "this must be a proper rotation, not a reflection"
  at compile time.
- **The corrected matrix** — `(x, y, z) -> (x, z, -y)`, replacing the old
  `(x, -z, y)` — is now independently corroborated a third way beyond §8's
  own headless-Blender empirical test and the hand-derived math: this
  repo's own `reference/wow.export` (already checked out, previously never
  mined for this) has its own, independently-written coordinate-conversion
  code for positions, normals, rotations, *and* scale, and matches the
  corrected formula exactly on every one of them (scale needed no change at
  all — already correct before this fix, see `TRANSFORM_TRIAGE.md` §4 for
  why that's informative about the bug's own shape). `reference/wow.export`
  is explicitly **not** treated as ground truth on its own (it's known to
  be flaky) — this is corroboration, not the basis for the fix.
- **A new, asset-agnostic regression test replaces "trust the math," and
  was proven to actually catch the historical bug, not just proven to
  pass**: `tests/test_conformance.cpp` builds a synthetic `gltf::Skeleton`
  (a root plus three children offset one unit along local X/Y/Z) with no
  dependency on any real M2 file, real body plan, or "up" convention for
  any particular asset type — the property under test is a pure
  round-trip-identity fact (an M2-local axis offset must land at the
  *identical* coordinate in Blender's own world space after a real
  husk-export → Blender-import round trip), not "does this look
  anatomically correct." Before trusting it, this test's own `kWowToGltf`
  matrix was temporarily reverted to the historical formula and rerun: it
  failed exactly as the root-cause math predicts (the `+X` probe, on the
  rotation's own invariant axis, still landed correctly; `+Y` and `+Z` both
  flipped) — then the fix was restored and reverified green. A second,
  explicitly non-load-bearing check (`_Name`, a real key-bone landmark)
  confirms the same thing on the real `bloodelffemale.m2` fixture, gated to
  skip cleanly on any model without that specific bone tagged. A new
  property-based unit test (`tests/test_gltf.cpp`) independently confirms
  `rotationZUpToYUp`'s own conjugation math is self-consistent for several
  real test rotations, regardless of which underlying matrix is used.
- **A real quadruped fixture** (`test_data/creature/wolf/wolf.m2`, 66
  bones/557 vertices) was added for pipeline-shape diversity — explicitly
  *not* additional orientation-correctness coverage (the synthetic probe
  above already covers any asset type by construction), but real coverage
  for a bone-hierarchy/body-plan shape `bloodelffemale.m2` doesn't
  represent, so a future *different* class of bug doesn't get the same
  one-fixture free pass this session's own bug did.
- **Full suite green with zero hand-updated literals**: every existing
  test that touches a position/rotation/scale value passed unmodified
  against the corrected formula (484/484 + 1 skip via `husk-tests`, 485/485
  via `ctest`) — informative on its own: nothing else in this codebase was
  silently depending on the old formula's specific wrong values.
- **Deliberately still open at the time, by design, not an oversight**: a
  real animated clip, visually confirmed in Blender's actual GUI viewport —
  Luna's own explicit "after all of it is tested and implemented i will
  verify... until then rely on headless Blender." Every check this session
  added is numeric; nothing here substituted for that last look. **Since
  resolved** (2026-08-08): Luna confirmed "Animation looks OK" in Blender's
  own GUI, closing out the investigation — `TRANSFORM_TRIAGE.md` itself was
  deleted once this was done, per this project's own "survey's job is done"
  lifecycle.

**Second follow-up, same day**: node/bone/material naming and texture
linking were manually confirmed working (a live export with a real
`--textures` directory showed 2/70 materials correctly embedding real
image bytes), which surfaced two real, previously-unflagged gaps, both
fixed the same session. (1) The render mesh's own glTF node had no `name`
at all in the common `--skin <path>` case, unlike bones (§6) and the
collision mesh — now falls back to the model's own basename. (2) Texture
resolution always preferred `<FileDataID>.png` outright for any slot with
a resolvable FileDataID, even when that specific file was missing and a
real, descriptively-named file (the shape a real extraction workflow --
`reference/wow.export`, or a `.blp` converted via `husk-blp` keeping its
own name -- actually produces) sat right there unclaimed; the fuzzy-match
fallback `BLENDER_EXPORT_TODO.md` §4 built was scoped to hardcoded slots
only. Now every slot tries embedded-filename, then FileDataID-exact (only
if the file is actually present), then the fuzzy pool, in that order —
deterministic matches are checked before ever touching the shared,
ambiguity-prone fuzzy pool, specifically to avoid a real regression an
earlier version of this fix introduced (an unrelated hardcoded slot
claiming a named file that actually belonged to a different,
FileDataID-resolvable slot processed later in the same batch loop — found
by a live test, not theorized, and now a permanent `tests/test_cli.cpp`
regression case). `gltf::Material::baseColorTextureFileDataId`
(`texture_file_data_id` extras) records the resolved FileDataID regardless
of which path actually supplied the image, so that traceability survives
even when a differently-named file wins.

**`skin::Submesh::indexStart` is a corrected 32-bit value, not the raw
on-disk 16-bit field — `Level` is address-extension bits, not LOD
metadata.** Found via a real interactive Blender render of
`bloodelffemale_hd.m2` showing a large chunk of missing body geometry
(hips/lower torso) — traced all the way back to `M2SkinSection::Level`
(offset 0x02, between `skinSectionId` and `vertexStart`), which every
earlier pass at this field name assumed was LOD-selection metadata and
left unread. Checking a real, independent working implementation
(`reference/wow.export`'s `Skin.js`) instead of assuming from the name
found the real behavior: `triangleStart += level << 16`. `.skin`'s
on-disk `indexStart`/`triangleStart` field is only 16 bits, so any model
whose resolved triangle-index buffer exceeds 65,535 entries needs `Level`
to address the back half of it — not rare: `bloodelffemale_hd00.skin`'s
buffer is 136,254 entries, and 77 of its 114 submeshes (68%) needed the
correction. Without it, husk silently sliced the *wrong* region of the
buffer for every affected submesh (aliased into the first 65,536 entries)
instead of throwing or visibly failing, so this went undetected through
every earlier headless-Blender/`gltf_validator` check this project has —
none of them compare *which* triangles got drawn against the model's own
full vertex pool, only that *some* valid-looking triangles exist.
`Submesh::indexStart` is now `uint32_t`, computed once in
`parseSubmeshes` as `(level << 16) | rawIndexStart`; callers never see
`Level` itself. Full account, including a first-pass "real content, not a
bug" conclusion that was wrong and got corrected: `EYES_ON_FINDINGS.md`
#5.

**Ambiguous hardcoded-texture-slot candidates (2+ same-basename files) are
now all embedded, not silently dropped.** Reversed from the
`BLENDER_EXPORT_TODO.md` §4 stance directly above (`report the count, embed
neither`) after direct pushback: the "no in-file candidate data" objection
that blocks *picking* a correct hardcoded-slot texture doesn't block
*enumerating* the candidates, since that list comes from husk's own
`--textures` directory scan (`FuzzyTexturePool`), not from M2 data — it's
already built by the time ambiguity is detected. Every real candidate is
now embedded as a genuine glTF image/texture
(`gltf::Material::AlternateTextureCandidate`, same "own image + extras
index" shape `additionalTextureLayers` already used), listed under
`alternate_textures` material extras; one arbitrary (alphabetically first,
deterministic) candidate is also wired in as the actual `baseColorTexture`
so the export renders as something by default rather than bare — the same
"export everything, let the client filter" treatment mutually-exclusive
geosets already get, applied here for the first time to texture data.
`husk export`'s own warning now names every embedded alternative and which
one was arbitrarily chosen as the default.

### Human-readable names for animations/geosets, and Blender's Asset Browser as an animation picker (2026-08-21)

Three related usability gaps, all found the same way: real DB2/glTF-extras
data husk already produces sat unconsumed by the one consuming tool
(`tools/husk_blender_geoset_mask.py`) that could turn it into something
visible. Tracked as `TODO/HUMAN_NAMES_TODO.md`.

**Geoset dropdown labels now use real customization-choice names when
available, not just `variant_<n>`.** husk already resolves every real
`ChrCustomizationOption`/`Choice` -> `GeosetType*100+GeosetID` mapping into
`chr_customization_options` skin extras (`chrcustomization_db2.hpp`,
`--customization-choice-ids`/`--chr-model-id`'s own auto-enrichment) — the
gap was purely on the Blender-script side, which built its Menu Switch
dropdowns from the plain `group_<n>,variant_<n>` vertex-group naming
convention alone. `build_geoset_choice_names` reshapes
`chr_customization_options` into `{group: {variant: choice_name}}` +
`{group: option_name}`; `_build_group_hidden_term` now labels each
dropdown item with the real choice name (falling back to `variant_<n>`
when no matching choice exists — creature models, or a group with no
player-facing customization at all, e.g. group 0's base body) and the
selector/interface-socket itself with the real option name. **Key
constraint that shaped this**: `NodeEnumItem` (Menu Switch's own dropdown
item type) has no separate identifier/label pair — `.name` is both at
once, unlike a `bpy.types.EnumProperty` triple. Renaming an item's `.name`
to a human string would have silently broken `default_item` matching
(`CURATED_DEFAULT_VARIANTS`/`enabled_geosets_to_default_overrides`, both
keyed by the stable `"variant_<n>"` string). Fixed by keeping
`default_item`'s own contract exactly as it was — still a `"variant_<n>"`/
`"none"` string — but resolving it against a `label_by_variant` map built
alongside the (now human) item names, rather than a direct string-membership
check. Verified end to end against real local data
(`bloodelffemale_hd.m2`, `--chr-model-id`/`--customization-choice-ids`):
6 of 23 real geoset groups on that model resolve a real option name, 30
variants across them resolve a real choice name; the other 17 groups (no
player-facing customization at all for that model, e.g. the base body/
face groups) keep the plain numeric fallback, unchanged from before.

**`husk export` now attaches AnimationData.db2's real Name column
("Stand", "Walk", ...) to a matching clip's own `sequence_metadata`
extras when `--db2-dir`/`--dbd-dir` are given — real, verified code, but
current real local data can't exercise it.** New `animationdata_db2.hpp`/
`.cpp` (same `db2table.hpp`-backed thin-wrapper pattern as every other
DB2 reader here), threaded through `buildSequenceMetadata`/
`buildAnimations` as a new `animation_data_name` field
(`SequenceMetadata::animationDataName`) — deliberately *not* folded into
the clip's own stable `anim_<id>_<variationIndex>` glTF `name` (every
other tool here, including this script's own `read_animation_clip_names`,
looks clips up by that name; a human name is enrichment, not a rename at
the husk layer — see the Blender-side asset-marking paragraph below for
where the actual rename happens). Verified correct via a synthetic WDC5
fixture that does carry a `Name` column
(`tests/test_cli_animationdata.cpp`) — but running against real local
data (`husk db2-info animationdata.db2`, build 12.1.0-era extraction)
found the real column simply isn't there any more: the resolved real
layout (`LAYOUT BBF66A3C`) is `ID/Fallback/BehaviorTier/BehaviorID/
Flags` only, no `Name` field at all. Cross-checked against
`reference/WoWDBDefs/definitions/AnimationData.dbd`: `Name` was a real
column through the 1.x-8.x layouts and is absent from every layout from
roughly 8.x onward — the same "client schema genuinely dropped this
column, not a husk/casc-tool extraction gap" class `CLAUDE_HISTORY.md`
already documented for `AnimationData`'s own `aliasNext`-name lookup.
Nothing to fix in husk here; the plumbing is real and correct, current
real data just can't feed it a name. `husk_blender_geoset_mask.py`'s own
`read_animation_clip_names` doc comment records this same finding for
anyone reading the Blender-side code without this file.

**Blender's own Asset Browser is now a real per-animation picker, via
`action.asset_mark()`.** husk's own animation clips were previously only
reachable by scrubbing the raw Action list (or an NLA track) by their
machine name. `mark_actions_as_assets` (new, `husk_blender_geoset_mask.py`)
marks every clip's imported Action as a real Blender asset after import,
matched by the clip's own glTF animation name
(`read_animation_clip_names`) so it only ever touches this model's own
actions, never pre-existing ones already in the scene. This is
deliberately the one place in this whole script that *renames* rather
than just annotating: a `NodeEnumItem`-style label doesn't exist for
Actions — the Asset Browser and the plain Action list both show `Action.name`
directly, so a human name has to live there to be visible at all. When
`read_animation_clip_names` resolves a real `animation_data_name` (see
above — not the current real-data case, but real once older/richer
`AnimationData.db2` data is available), the Action gets renamed to it;
the original machine name is preserved as the asset's own description
either way, so "which real `anim_<id>` was this" is never lost to a
rename. Verified end to end against real local data
(`bloodelffemale_hd.m2`, 338 real clips): all 338 marked as assets, 0
renamed (consistent with the `AnimationData.Name` finding above — nothing
to rename to yet).
