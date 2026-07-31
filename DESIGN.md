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
`TODO_correctness.md`; a granular per-M2-feature completion breakdown
(parse depth vs. consumption depth vs. glTF ceiling) lives in
`M2_COMPLETENESS.md`.

## Goal

A real Blender import path for modern (Legion+, chunked) WoW M2 models —
mesh, skeleton, textures, materials, animation — via a correct glTF 2.0
export (`.glb`, core PBR metallic-roughness) that Blender's own built-in
importer opens unmodified. husk's job stops at "read WoW formats, write
correct glTF"; Blender-side concerns (addon UI, live reimport) are out of
scope unless glTF itself proves insufficient.

Non-goals, by design, not oversight:

- No CASC/listfile access, ever. husk never resolves a FileDataID to a real
  WoW install path. A separate tool (e.g. `casc-tool`) extracts real files
  first; husk only reads what's already on disk, and sidecar resolution
  (`--skin-dir`/`--textures`/`--anim`/`--bones-dir`) is a **local-directory,
  FileDataID-named** convention the user populates themselves — never CASC.
  Generalizes past raw file extraction, too: an out-of-band tool scraping
  CASC/DB2 at build time to learn something husk itself can't (e.g. which
  `.bone` slot a customization choice selects, `TODO_correctness.md` #6) is
  fine — it would just hand husk a plain local file/flag to read, same as
  every other sidecar. What husk itself never does, at runtime, under any
  circumstance, is talk to CASC/DB2 directly.
- No write-back to WoW's native formats. glTF-out only.
- No WMO or M3 support (tracked, not started). A full-storage `M3DT`-magic
  scan (casc-tool, product `wow` build 68887, 1,891,552 files) found 8 real
  `.m3` files exist in this corpus (unresolved listfile names, `models\
  unknown\unk_exp*\<fdid>.m3`) — noted for the record, still out of scope,
  no investigation started.
- No ADT (terrain) support yet either (tracked, not started, `README.md`'s
  format matrix gained an ADT column 2026-07-31) — genuinely missed when
  this project was first scoped, not a deliberate exclusion: crucial for
  any actual rendered world, not just props/buildings. `.wdt`/`.wdl` (which
  ADT tiles exist per map; coarse whole-continent distant heightmap) are
  ADT's own real dependencies, not yet scoped either.
- **PM4/PD4 (server-side navigation/pathing mesh) declared in scope**,
  2026-07-31, explicitly for pathing use cases ("we want pathing") — not
  yet researched at all (no wiki page read, no real file inspected this
  session). Genuinely different in kind from every other format above:
  never touched by the client renderer, only by server-side movement/AI,
  so "renders correctly" isn't the bar for it the way it is for M2/WMO/
  ADT — a future investigation should ground itself in that distinction
  before assuming the M2/WMO byte-verification playbook transfers as-is.
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
published spec anywhere (`WIKI_FINDINGS.md` §2's follow-up has the full
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
`WIKI_FINDINGS.md` §1. This is the model for how to resolve any future
spec ambiguity here: decode real records at every plausible stride/shape
and check for garbage, don't guess from text alone.

**Bone rotation/scale keyframes get their own Z-up→Y-up conversion,
distinct from position's `zUpToYUp`.** No wowdev.wiki formula exists for
this specific step. Derived from the general change-of-basis rule for a
proper-rotation basis change (`zUpToYUp`'s `(X,-Z,Y)` permutation has
determinant +1): a rotation quaternion's vector part gets the same
permutation (scalar/`w` untouched); a scale vector gets the same
permutation with signs dropped. Checked numerically against several test
rotations (bit-identical conjugated-matrix vs. permutation-shortcut
results), not taken on faith — this is weaker evidence than a cited spec
formula and depends on the assumption that WoW bones use plain
parent-relative TRS with no separate pivot concept beyond
`M2CompBone.pivot`.

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
close itself: *which* `M2SkinSection.skinSectionId` variant is "correct"
for a given character depends on DBC/DB2 data (`CharacterSections`,
geoset groups) husk deliberately never reads (no CASC/listfile access is a
hard non-goal, see Non-goals above); and a batch's additional texture
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

**A batch's `M2TextureTransform` (UV scroll/rotate/scale) is surfaced as
`extras`, never a real `KHR_texture_transform`.** Same "tag it, don't guess
at semantics" family as geoset selection/multi-texture-layer rendering
above, for a different reason: core glTF's `KHR_texture_transform`
extension is itself *static* (offset/rotation/scale baked once, no
animation-channel target), so even a correct translation couldn't play
back the animated case — and a real texture transform is almost always
animated in practice (scrolling lava/water is the model's whole reason to
carry one). The constant case *could* in principle become a real static
`KHR_texture_transform`, except wowdev.wiki's own note that rotation here
pivots around the texture's center (0.5, 0.5), not the extension's own
(0,0) origin, means a correct translation has to fold that pivot
difference into the extension's offset field — exactly the kind of
byte-level claim this project's own methodology (`WIKI_FINDINGS.md`: decode
real records, don't guess from text alone) says shouldn't ship unverified,
and no real texture-transform-carrying file has turned up in this
project's test data yet to check it against. `m2::parseTextureTransforms`
resolves the same constant-vs-animated split `constantTrackValueOffset`
already does for `M2Color`/`M2TextureWeight`; `gltf::Material::
textureTransform` carries whichever raw values resolved through to
`extras`, untransformed, for a downstream renderer or Blender script that
wants to apply the pivot correction itself.

**A texture's `M2Texture::type` (nonzero -- a hardcoded/replaceable slot)
is surfaced as `extras`, distinguishing "husk can't resolve this locally"
from "the `--textures` directory just didn't have the file."** `type == 0`
("NONE") is a real, filename/FileDataID-based texture, resolvable the
ordinary way; any other value means the client substitutes the real image
at runtime from DBC-driven character-customization/item-tint data husk has
no access to, per this file's Non-goals -- so an empty `baseColorImagePng`
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
husk has no access to and, per this file's Non-goals, never will. Real
investigation (`WIKI_FINDINGS.md` §4's follow-up) ruled out the two more
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
errors (`WIKI_FINDINGS.md` §9). Chunk tags are byte-reversed on disk (WMO/
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

**BLP/texture conversion is a separate Python process, permanently.** Two
independent reasons: real DXT/BC block decoding needs library maturity
(Pillow) C++ doesn't have an equivalent of already in this project, and the
process boundary is kept even now that `--textures` embeds real PNGs —
husk reads a file `husk-blp` already wrote, it never invokes `husk-blp` or
links against Pillow.

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
chain-resolved into a real animation clip** (`WIKI_FINDINGS.md` §12).
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
niche sidecar-shaped data rather than core render geometry. Its four
regions (`vertexPositions`/`faceNormals`/`indices`/`flags`) are each an
independent `(count, offset)` pair read via its own offset, never
accumulated sequentially the way `.phys`'s `PLYT` header+data walk is — the
wiki's own "there can be extra bytes between the data" warning is real,
confirmed on a real file with an 8-byte gap between `faceNormals` and
`indices`. Verified against all 2,354 real `PCOL`-bearing files in the
local corpus (corrected from an earlier scanner bug's false "zero real
files," `WIKI_FINDINGS.md` §10): every region in-bounds, every index in
range for that file's own vertex count, `indexCount == faceNormCount * 3`
on all 2,354 (triangle triples, one normal per triangle — the same shape
M2's own core collision mesh already has). `flags`' per-record meaning is
undocumented on the wiki — exposed raw, not guessed at.

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
   exactly — see `WIKI_FINDINGS.md` §5 for the bounding-box finding. See
   `README.md`'s Testing section for the full per-check writeup, including
   two real Blender-importer-side contamination sources found while making
   these checks exact.

Known gap: the ad hoc real-`.anim`-directory verification described in the
README's roadmap stage 6 (50/50 and 54/54 real files, parsed back apart in
Python) has no `HUSK_TEST_ANIM_DIR`-gated repeatable test yet — it doesn't
re-run automatically. Tracked as a testing debt, not a correctness bug.

## Open work

See `TODO_correctness.md` for the current punch list (`M2Camera`, `.bone`
slot selection, and two awareness-only footnotes) and `WIKI_FINDINGS.md`
for every real-data-driven spec correction found so far, `AFSB`'s
included. `TODO_correctness.md`/`WIKI_FINDINGS.md` are living documents;
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
false), not a real result -- corrected: all four (plus `PCOL`, now
implemented -- see Key design decisions below) are real and present,
cross-checked independently via `casc-tool scan-chunks` (`WIKI_FINDINGS.md`
§10);
`DETL`'s real byte layout is fully resolved (12-byte stride, zero-padded to
a 16-byte boundary -- §11); `aliasNext` is a local `sequences`-array index,
not an external id, at the `M2Bounds`-corrected offset 0x3E (§12), now
implemented end to end (see Key design decisions below).

`.phys` physics/collision sidecar support used to have its own living plan
here too (`PHYS_TODO.md`) -- now fully implemented (`src/phys.hpp`/
`phys.cpp`, `husk export --phys`, `husk dump-chunks <file.phys>`) and folded
into this file's own Key design decisions (the `.phys`-anchor/dump-chunks-
split entry above) and `WIKI_FINDINGS.md` §9's "Where these live in husk"
row, so the standalone file was removed. Its own coverage table (verified
vs. unverified per chunk type, `WIKI_FINDINGS.md` §9) is unaffected — real
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
the external data source `TODO_correctness.md`'s `.bone`-slot-selection
gap and this file's `Texture.type` non-goal both point at) and broader
format scope (WMO/ADT/M3), but its own M2 loader has zero code path at
all for ribbons, particles, events, lights, cameras, or the `.phys`
sidecar — confirmed by reading its source directly (dead
`// this.data.move(8)` skip-comments, and no `PHYSLoader.js` file exists),
not inferred. Also surfaced 3 real corpus files with a genuinely new,
not-yet-tracked animation-data failure shape (NaN keyframes / a ~2.2
billion ms backward timestamp jump) — see that file's own closing
section for the exact files.
