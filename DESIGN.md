# DESIGN.md — husk

Architecture and design rationale. Status/progress lives in the README's
roadmap and format matrix, not here — this file explains *why* the code is
shaped the way it is, so a structural change can be checked against the
reasoning instead of just the current state. Real-file reverse-engineering
findings live in `WIKI_FINDINGS.md`; open correctness gaps live in
`TODO_correctness.md`.

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
  (`--skin-dir`/`--textures`/`--anim-dir`) is a **local-directory,
  FileDataID-named** convention the user populates themselves — never CASC.
- No write-back to WoW's native formats. glTF-out only.
- No WMO or M3 support (tracked, not started).
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
   │                    materials, attachments/events/lights, ribbons;
   │                    M2Track resolution (per-sequence, constant-value,
   │                    external-blob splicing)
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
   ribbons, sidecar FileDataID chunks (SFID/AFID/BFID/PFID/SKID/TXID).
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
   no `0x40`) from a `--anim-dir`-resolved `.anim` file via the model's (or
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

**`.anim` files carrying an `AFSB` chunk are skipped outright, regardless of
whether `AFM2` is also present.** For `.skel`-sourced models, real per-bone
data lives in undocumented `AFSB`, not `AFM2` — a small always-zeroed
`AFM2` "stub" can be present alongside it and must not be mistaken for real
track data (confirmed via a real bounds error when tried anyway). See
`WIKI_FINDINGS.md` §2 for the full byte-shape investigation. `AFSB`'s own
layout is unreverse-engineered — this is a detect-and-skip, not a
best-effort parse.

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
hypothetical), see `FAILURES2.md` #2.

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

**BLP/texture conversion is a separate Python process, permanently.** Two
independent reasons: real DXT/BC block decoding needs library maturity
(Pillow) C++ doesn't have an equivalent of already in this project, and the
process boundary is kept even now that `--textures` embeds real PNGs —
husk reads a file `husk-blp` already wrote, it never invokes `husk-blp` or
links against Pillow.

## Boundaries (where foreign data enters)

- Model file bytes (`.m2`) — chunk container + fixed-offset header/arrays.
- `.skin` sidecar — triangle-index lookup, submesh/batch structure.
- `.skel` sidecar — external bones + sequences.
- `.bone` sidecar — per-bone correction matrices (reverse-engineered).
- `.anim` sidecar — external per-sequence keyframe blob (`AFM2` flat
  format only; `AFSB` detected and rejected).
- `--textures`/`--skin-dir`/`--anim-dir` directories — user-populated,
  FileDataID-named, local filesystem only. Never CASC.
- `.blp` texture files (separate `blp/` Python tool) — container + mip
  table hand-rolled, block decode delegated to Pillow via synthetic DDS.

Every boundary above is read via explicit bounds-checked parsing with
named offsets, and every ambiguous byte-layout question is resolved against
real decoded file data before being trusted (see `WIKI_FINDINGS.md` for the
receipts) — never guessed from prose alone when a real file was available
to check against.

## Testing architecture

Three tiers, same shape used by `casc-tool`:

1. **Pure-logic** (`test_chunk`/`test_m2`/`test_skin`/`test_skel`/
   `test_gltf`) — synthetic buffers built field-by-field from the wiki
   spec (or, for `gltf`, round-tripped through tinygltf's own loader), no
   real files, always run.
2. **Command-layer** (`test_cli`/`test_dump`) — spawns the real compiled
   binary against small synthetic fixtures; exercises argv parsing and
   `cmd_*.cpp` exception handling without needing real game files.
3. **Integration** (`test_integration`) — the compiled binary against real,
   game-extracted files, gated behind `HUSK_TEST_*` env vars, skipped (not
   failed) when unset. Asserts on shape ("found some vertices," "plausible
   `.glb`"), never on one specific model's exact field values — those
   belong in tier 1.

Known gap: the ad hoc real-`.anim`-directory verification described in the
README's roadmap stage 6 (50/50 and 54/54 real files, parsed back apart in
Python) has no `HUSK_TEST_ANIM_DIR`-gated repeatable test yet — it doesn't
re-run automatically. Tracked as a testing debt, not a correctness bug.

## Open work

See `TODO_correctness.md` for the current punch list (particles, `AFSB`
reverse-engineering, `M2Camera`) and `WIKI_FINDINGS.md` for every
real-data-driven spec correction found so far. Both are living documents;
this file describes the shape of the system they operate within, not their
current item-by-item status.
