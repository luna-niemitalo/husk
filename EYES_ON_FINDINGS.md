# EYES_ON_FINDINGS.md

Findings from a real interactive Blender inspection of a `husk export` output
(post-`TRANSFORM_TRIAGE.md` orientation fix). Mesh completeness and
orientation are both confirmed good now. Four items were originally raised;
the texture-conversion-workflow item is resolved (BLP2 decode is now
embedded directly in `husk export`, see `DESIGN.md`/`README.md`) and removed
from this list. The remaining three below are investigated against the
current source, not guessed at.

## 1. Unnamed bones (`bone_29`) — real source-data limitation, already partially solved

**Symptom**: `ForearmR`/`ForearmL` get real names; other bones (e.g. the
wrist) fall back to `bone_29`.

**Grounding**: M2 has no free-text per-bone name table at all — the only
name data available is `keyBoneId` (`src/m2_skeleton.hpp:36`), a
back-reference into a fixed ~193-row wowdev.wiki enum of "well-known" rig
bones (`src/m2_header.cpp:46-56`, `keyBoneName`). Most real bones — wrists,
fingers, twist bones, face-driver bones, cloth-sim helpers — simply have no
entry in that enum (`keyBoneId == -1`), not because husk failed to read
something. `src/export_skeleton.cpp:59-63` sets the glTF joint name from
`keyBoneName` when present; `src/gltf_skeleton.cpp:127` falls back to
`"bone_<index>"` otherwise. Already verified and documented against the
real `bloodelffemale.m2` fixture: 14 of 119 bones get a real name, the
rest fall back — "a real fact about this fixture, not a bug in the
lookup."

**Verdict**: not a gap to close in husk. The wrist specifically not having
a key-bone slot is upstream WoW rig data, not a husk omission — Blizzard's
own key-bone table has no "Wrist" entry at all (it jumps hand → forearm →
shoulder). `bone_<index>` is the correct, already-intentional fallback.

## 2. Root bone (bone 0) holding most of the mesh's weight — real M2 data, faithfully passed through

**Symptom**: bone_0 appears to own the entire body's skin weight, while
many bones with index > 50 show zero vertex influence, and no individual
bone owns the face.

**Grounding**: reproduced directly by rebuilding `bloodelffemale.m2` and
inspecting the exported `.glb`'s `JOINTS_0`/`WEIGHTS_0` accessors: 51 of
119 bones carry any nonzero weight at all, and bone 0 alone holds ~67% of
weighted vertex-influence slots (380,310 of 564,270) — consistent with
what was observed in Blender.
`boneWeights`/`boneIndices` are read byte-for-byte from the M2 vertex
record at fixed offsets (`src/m2_skeleton.cpp:35-36`) and passed straight
through in `buildSkinning` (`src/export_skeleton.cpp:90-105`,
`jw.joints[j] = v.boneIndices[j]; jw.weights[j] = v.boneWeights[j] /
255.0f;`) — the only guard is a bounds check that *throws* on an
out-of-range index, never one that zeroes or defaults it. There is no code
path in husk that could collapse weights onto bone 0.

This also matches a deliberate design choice already recorded in
`TODO_correctness.md:80-92`: husk writes full per-vertex *global* joint
indices straight from the M2, bypassing `.skin`'s hardware bone-palette
remap tables (`boneCombos`/`boneLookup`/`boneComboIndex`, `src/skin.hpp:38`)
entirely — those exist for GPU bone-palette batching/LOD, not needed when
global indices are written directly.

**Verdict**: real M2 rig data, not a husk bug. A single low-index bone
(root/pelvis/torso) owning the bulk of a rigid body mesh while many
high-index bones (per-attachment sockets, face-driver bones, cloth-sim
helpers unused by *this* geoset combination) sit at zero is a normal WoW
rigging pattern — WoW characters lean on hardware bone-palette selection
per draw batch precisely so unused bones can go unweighted for a given
geoset/LOD. The absence of a dedicated face bone is consistent with WoW
faces historically being static geometry (or animated via texture/UV
tricks and separate facial-bone sequences on some models), not proof of a
missing husk feature. Worth an independent sanity check against a known
facially-animated model if one is added to `test_data/`, but nothing in
the current code path is suspect.

## 3. Material naming granularity — usability gap, real duplicates confirmed

**Symptom**: dozens of materials named `batch<N>_mat3_tex0_skin` for
varying `N`, hard to tell apart; ~23-25 of them look identical.

**Grounding**: `src/export_materials.cpp:234` creates one `gltf::Material`
per `.skin` batch unconditionally — no merge/dedup step exists anywhere in
`src/`. The name is built from `batch<index>_mat<materialIndex>` (:281),
then `_tex<textureIndex>` (:374), then `_<textureTypeName>` (:388), then an
optional filename/FileDataID suffix. Material identity, as far as husk
models it, is: `mat.blendMode` (:278), `mat.flags` (:279-280,
two-sided/unlit), and the resolved texture index/type (:359-393);
`M2Batch.shader_id` is deliberately not read at all (`src/skin.hpp:60`,
`src/skin.cpp:172`). Rebuilding a real export confirmed 28 batches all
named `..._mat3_tex0_skin`, all referencing the identical
`m2.materials[3]`/`m2.textures[0]` by construction — true duplicates in
every field husk currently models, differing only in which submesh/geoset
batch draws with them (e.g. separately-toggleable skin geosets sharing one
base material, which is exactly the kind of per-geoset selection
`M2_GAPS_TODO.md`'s now-closed `skinSectionId`-extras work was about).

**Verdict**: genuine usability gap, not a correctness bug — every one of
those materials is already correct, just redundant. Two independent
improvements, not mutually exclusive:

- **Dedup**: when two batches resolve to the same
  (`materialIndex`, `textureIndex`, `blendMode`, `flags`, `textureType`)
  tuple, emit one glTF material and have both primitives reference it,
  instead of N structurally-identical materials. Straightforward, and
  shrinks both the material list and the `.glb` JSON.
- **Naming**: even without dedup, `mat<materialIndex>_tex<textureIndex>`
  (dropping the per-batch `batch<N>` prefix, or moving it to `extras`
  rather than the display name) would already make the ~25 duplicates
  visibly identical instead of visibly distinct in Blender's material
  list.

Neither is implemented yet — this is a finding, not a fix.

## 4. `husk info`/`export` given a non-M2 file (e.g. a `.skin` directly) fails with a confusing byte-garbage-looking error, not a clean "wrong file type"

**Symptom**: `husk info bloodelffemale_hd_sdr02.skin` (a real `.skin` file
passed directly, not via `--skin`) fails with:
```
husk: couldn't read '...bloodelffemale_hd_sdr02.skin': chunk '+<0xc8>+<0xc9>'
at offset 22479 declares size 3408644651 but only 310969 bytes remain in
the buffer
```
which reads like memory corruption or a garbled file, not "wrong argument."

**Grounding**: traced byte-for-byte, not guessed at. `src/m2_primitives.cpp`'s
`resolveBlob` checks the first 4 bytes for `MD20` (pre-Legion M2); if that
fails, it unconditionally assumes the file is a Legion+ *chunked M2* and
calls `readChunks` (`src/chunk.cpp`) over the *entire* file looking for an
`MD21` chunk. A `.skin` file's own real magic is `SKIN` (`src/skin.cpp`'s
`parseHeader`, unrelated to M2's chunk system — `.skin` is a flat header,
never chunked), but `SKIN` + the next 4 bytes (`0xc7 0x57 0x00 0x00` =
22471) *happen to look like* a syntactically valid chunk tag+size pair to
`readChunks`, which knows nothing about `.skin`'s real layout. It
"successfully" consumes this fake chunk (8-byte header + 22,471-byte
payload = 22,479 bytes, landing exactly on the reported offset), then tries
to read the *next* chunk header at offset 22479 — which is really the
middle of the `.skin` file's own vertex/index array data, not a chunk at
all. `readChunks` reads 4 arbitrary bytes there as a "size" field (decoding
to the nonsensical 3,408,644,651) and throws. The friendly, already-existing
fallback message ("file doesn't start with MD20 and has no MD21 chunk")
never gets a chance to fire, because `readChunks` throws *while still
enumerating* chunks, before `resolveBlob` can check whether `MD21` was
among them.

**Verdict**: real usability gap, not data corruption or a `.skin`-parsing
bug — the `.skin` file itself is fine (confirmed: its real `SKIN` header
parses correctly via `src/skin.cpp`'s own dedicated, non-chunked parser).
This is `husk info`/`husk export`'s M2-loading path (`resolveBlob`) having
no cheap up-front "does this even look like an M2 file" check before
committing to a full chunk-enumeration pass — any non-M2 file whose first 8
bytes happen to decode as a plausible tag+size pair will produce this same
class of misleading, alarming-looking low-level `ChunkError` instead of a
clear "this isn't an M2 file" message. `husk info`/`export` only ever
documented `.m2` as their direct argument (`.skin`/`.skel` are meant to be
passed via `--skin`/`--skel`, `.bone`/`.phys` are the only sidecars
`dump-chunks` accepts directly) — so this was a real user mistake, but the
tool's failure mode actively obscures that instead of naming it.

**Action** (not yet implemented, no code changed this session): validate
the second 4-byte tag against `MD21` immediately after a failed `MD20`
check, before calling `readChunks` at all — or have `readChunks` bail out
with a distinct, clearly-worded error the first time a chunk's declared
size would run off the end of the buffer *and* look up the file's actual
first-4-bytes magic to report what was really found (e.g. "expected an M2
file (MD20/MD21), got a file starting with 'SKIN' -- did you mean to pass
this via --skin instead?"). Either fix is small; neither is implemented
yet — this is a finding, not a fix.

### DEVELOPER NOTE:

Fix by making it possible read just the skin file, and print the magic bits and all of the data we get access to from just that file, so the info tool can be used as an independent exploration tool, without having to always target the m2 file

## 5. `bloodelffemale_hd.m2`/`bloodelffemale_hd00.skin` was missing its whole lower-body/hip geometry — real husk bug, found and fixed (`Submesh::Level`)

**Symptom**: rendering a real, unmodified `husk export` of the HD model
(true bind pose, materials stripped to rule out shading) in headless
Blender shows what looks like a disconnected upper body/head floating
above a separate pair of boots, joined only by a thin sliver of geometry.

**Grounding**: this is not the collision-hull-occlusion bug (item 3's
predecessor, now fixed by making `--collision` opt-in) and not a skinning/
`.skel`-bone-mapping bug — both were checked and ruled out directly:

1. A full per-vertex-group centroid-vs-bone-position sweep (every one of
   245 bones, true rest pose: auto-assigned action cleared, every pose bone
   zeroed) found no group deviating more than 0.22 units from its
   controlling bone's real position — nowhere near enough to explain a
   ~1-unit visual gap, and consistent with the skin-index/`.skel`-mapping
   correctness already established elsewhere in this investigation.
2. A direct histogram of every evaluated vertex's world-space Z coordinate
   (in 0.1-unit bands) shows a **real, exact gap: zero vertices between
   Z=0.70 and Z=1.00** (out of 35,377 total) — every other band from -0.1
   up to 1.7 is populated. This is missing geometry, not misplaced
   geometry: no amount of skinning-transform error would produce a clean
   empty band with populated bands on both sides.
3. The same check against the SD model (`bloodelffemale.m2` +
   `bloodelffemale00.skin`, the fixture validated repeatedly all session,
   and which renders as one continuous, correct-looking body) shows a
   **fully continuous** Z distribution, every 0.1-unit band from -0.1 to
   1.7 populated — no gap. Same character, same body region, no gap on SD.
4. Re-checked against `hd_test.glb` (an export made earlier in this same
   session, before any of today's code changes) and got an **identical**
   gap — ruling out today's `--collision` change or anything else edited
   this session as the cause. This is a pre-existing property of husk's
   handling of this specific `.m2`/`.skin` pair, not a regression.

**Verdict**: real and reproducible, but **not root-caused yet** — two
live hypotheses, not distinguished:

- A genuine WoW HD-asset-data property: the HD body mesh may simply not
  model bare upper-leg/thigh skin at all (relying on an always-equipped
  legwear layer that this specific `.skin`/geoset selection doesn't
  happen to include), unlike the older SD asset which might model a more
  complete "always visible" base body.
- A real husk `.skin`/geoset-resolution gap specific to the HD file's
  batch structure (113 geoset IDs span this file, vs. SD's 66) — e.g. a
  batch or geoset ID that should cover this Z band failing to resolve or
  being silently dropped somewhere in `resolveTriangleIndices`/
  `parseSubmeshes`/`parseBatches` (`src/skin.cpp`).

**Root cause, now confirmed — this is a real husk bug, not missing asset
data.** Ran the distinguishing test proposed above, via a small standalone
scratch program linked against `libhusk-lib.a` (not committed, scratchpad
only):

1. The M2's own full vertex pool (195,498 vertices, unfiltered by any
   `.skin`) has real geometry in the "missing" Z=0.70–1.00 band: 5,947
   vertices there.
2. `bloodelffemale_hd00.skin`'s own resolved triangle-index buffer
   (`skin::resolveTriangleIndices` — the flat, submesh/batch-independent
   `vertices[indices[i]]` resolution `skin.hpp` documents) references
   **1,012** of those same-band vertices directly, by real global M2 vertex
   index. The `.skin` file itself unambiguously wants to draw triangles in
   this band.
3. Yet the actual `husk export` output has **zero** vertices there (the
   original Z-histogram finding above). Something between step 2's
   resolved triangle buffer and the final glTF primitives drops this data.

This points squarely at `buildMaterialsAndPrimitives`
(`src/export_materials.cpp`) iterating `.skin`'s **batches**, not its
**submeshes** — `skin::resolveTriangleIndices` doesn't care about either,
it's a flat resolution of the whole index buffer, but the actual per-
primitive export loop only ever emits geometry for a submesh that has a
batch drawing it (`Batch::skinSectionIndex -> Header::submeshes`,
`skin.hpp`'s own doc comment). If the submesh(es) covering this Z band
have no batch pointing at them in this particular `.skin` file — for
whatever reason, real content choice or a real resolution bug — their
triangles never reach `buildMaterialsAndPrimitives`'s loop even though
`resolveTriangleIndices` (used elsewhere, e.g. bounds-checking) sees them
fine.

**Correction after a more careful check — reverses the conclusion above.**
The first batch-association pass was itself wrong: it checked each
submesh's raw `vertexStart`/`vertexCount` pool (which vertices a submesh
*could* reference), not the actual triangles it draws
(`indexStart`/`indexCount`, sliced from the resolved triangle-index
buffer — exactly what `buildMaterialsAndPrimitives` uses for
`prim.indices`). Redone correctly: **zero** of the 114 submeshes' real
index ranges reference a gap-band vertex, and cross-checked independently
against the actual exported `.glb` via `tinygltf` (bypassing Blender
entirely) — 0 of 136,254 index-buffer entries reference the gap band
there either. Two independent readers (tinygltf and Blender) agree the
`.glb` husk wrote has no triangles there. So the search moved to *why*,
and found the real answer: **70,482 of the `.skin`'s 136,254 resolved
triangle-index-buffer slots (52%) aren't covered by *any* submesh's
`indexStart`/`indexCount` range at all** — including all 4,680 gap-band
references. This is far too large a fraction to be an accidental husk
parsing bug (a real bug would drop a handful of triangles, not half the
buffer); it's much more consistent with a real, unremarkable `.skin`
structural fact — spare/padding buffer capacity, or triangle data really
meant for a different submesh table (a different LOD tier's `.skin`,
`bloodelffemale_hd_lod01.skin` etc. exist alongside `..._hd00.skin` in a
real CASC export) that this specific LOD-0 file's own submesh table simply
never claims.

**Checked every other real `.skin` variant sitting alongside `..._hd00.skin`
in a real CASC export** (`bloodelffemale_hd_lod01/02/03.skin`,
`bloodelffemale_hd_sdr00.skin`) — every single one showed the identical
pattern, a large unclaimed region of its own resolved triangle-index
buffer that includes the same gap band (1,012 / 1,012 / 622 / 578 gap-band
references respectively, all unclaimed by any submesh in that file). This
was taken, at the time, to mean the gap was "claimed by a sibling LOD tier
instead" was ruled out and the geometry was simply never authored —
**this was wrong**, called out directly and correctly by Luna: WoW
characters visibly render bare skin under unequipped gear slots in the
real game, every player has seen this, so "this asset never had it" was
never a plausible conclusion to land on without checking harder. The
"52% of the buffer is unclaimed, too large to be a bug" reasoning that led
there was itself the mistake — a systematic bug affecting *most* of a
model's submeshes looks exactly like "half the buffer is spare capacity"
from the outside if you stop at aggregate statistics instead of asking
why.

**Real root cause, found by reading a working independent implementation
instead of assuming**: `wow.export`'s own `Skin.js` (`reference/wow.export/
src/js/3D/Skin.js`) does `subMeshes[i].triangleStart += subMeshes[i].level
<< 16` when loading a `.skin`'s submesh table. `skin::Submesh::Level`
(offset 0x02, between `skinSectionId` and `vertexStart`) is **not**
LOD/culling metadata — despite the field name, and despite husk's own doc
comment lumping it in with "LOD/culling/skinning-optimization concerns"
and discarding it unread, on an assumption never actually checked against
real client behavior. It's the **high 16 bits of `indexStart`**
(`triangleStart` in wowdev.wiki's own naming): `.skin`'s on-disk
`indexStart` field is only 16 bits, so any model whose resolved triangle-
index buffer exceeds 65,535 entries needs `Level` to address the back half
of it at all. `bloodelffemale_hd00.skin`'s buffer is 136,254 entries —
**77 of its 114 submeshes (68%) have `Level=1`** and were being silently
misread, aliased into the wrong (first-65,536-entry) region of the buffer
instead of throwing or visibly failing. That's the real reason "half the
buffer looked unclaimed": it wasn't unclaimed, husk was looking for those
submeshes' triangles in entirely the wrong place.

**Verified as the actual, complete fix**: applying `(level << 16) |
rawIndexStart` when parsing `Submesh::indexStart` (`src/skin.cpp`,
`src/skin.hpp` — `indexStart` widened `uint16_t` → `uint32_t` to hold the
corrected value) brings the unclaimed-buffer count from 70,482/136,254 all
the way to **0/136,254** — every single slot now accounted for, gap band
included. A real headless-Blender render of the fixed export shows a
complete, coherent, fully connected blood elf female: torso, hips, robe,
boots, ears, hair, nothing floating, nothing missing. Full test suite
green (519/519) with a new dedicated regression test
(`tests/test_skin.cpp`, `Level (offset 0x02) is folded into indexStart's
high 16 bits, not discarded`) using the exact real values found on
`bloodelffemale_hd00.skin`'s own submesh #47. This is a real, previously-
undiscovered husk bug affecting **any** model whose `.skin` resolves to
more than 65,535 triangle-index entries — not an HD-specific or
character-specific issue, just first noticed on one.

**Lesson, stated plainly**: a field's name and a wiki page's own
categorization of it are not verification. `Level` was dismissed three
times across this project's history on the strength of its name alone;
the actual answer was sitting in a working reference implementation
(`reference/wow.export`, already cloned in this repo) the whole time.

## 6. `alternate_textures` (this session's own new feature, item 3 above) caused a real 1.9GB/5m39s export — fixed, plus a related usability gap found while investigating

**Symptom**: `husk export` with full auto-resolution against a real
`bloodelffemale_hd.m2` + real CASC-export texture directory produced a
1.9GB `.glb` and took 5m39s, despite every individual source file being
under 200MB. Confirmed reproducible, not a one-off (same command, same
result). Reported alongside a second observation: many of the embedded
`alternate_textures` images "look like the same image, slightly different
sizes" when browsed in Blender.

**Root cause of the size/runtime blowup**: this real export has **19
separate materials**, each a genuinely ambiguous hardcoded texture slot
sharing the **same 94-file candidate pool** (item 3's own new
`alternate_textures` feature, added earlier this session). The
implementation embedded every candidate's full bytes independently *per
material*, with no cross-material sharing — so the same 94 files got
read, BLP-decoded, and embedded **19 times each** (1,786 total redundant
decode+embed operations for what should have been 94). This is a direct,
self-inflicted regression from that same-session feature, not a
pre-existing bug.

**Fixed two ways, matching the two real costs**:
1. `src/export_materials.cpp`: `buildMaterialsAndPrimitives` now caches
   ambiguous-candidate bytes by path (`ambiguousCandidateCache`), read and
   BLP-decoded once regardless of how many ambiguous materials reference
   the same file — fixes the CPU/runtime cost.
2. `src/gltf_mesh.cpp`: `emitMaterial` now takes a shared
   `alternateTextureCache` (filename → already-created glTF texture
   index), threaded through `emitMeshNode`/`writeGlbMulti`
   (`src/gltf.cpp`) so a candidate that's already been embedded as a real
   glTF image/texture gets *referenced* by every other material that
   shares it, not duplicated — fixes the actual output file size, which
   the read/decode cache alone wouldn't have touched (tinygltf's
   `images`/`textures` arrays would still have held 19 separate copies of
   identical bytes without this).

**Verified on the same real export**: **1.9GB → 104MB** (~18x smaller,
matching the ~19-material duplication factor almost exactly), **5m39s →
5.4s** (~63x faster). Full test suite green (519/519) throughout — no
existing test exercised more than one ambiguous material sharing a pool at
once, so this class of bug had no coverage; worth a real regression test
if this code is touched again (a synthetic fixture with 2+ ambiguous
materials sharing one candidate pool, asserting the `.glb`'s `images`
array has one entry per distinct filename, not one per material).

**The second observation — investigated, not a decode bug**: cross-checked
directly, not assumed:
1. The real source `.blp` files are genuinely distinct — `md5sum` across
   60+ real candidate files in the CASC export directory found zero
   collisions.
2. husk's own embedded C++ decoder output was extracted directly from the
   fixed `.glb` (via a small `tinygltf`-linked scratch tool) and compared
   against the independent Python reference decoder (`blp/`'s `husk-blp`)
   for the same source file — **byte-identical visual result**, confirmed
   by eye. husk's decode is correct, at least for the file checked.
3. Two files sharing the same `skin_color`-prefixed naming convention
   turned out to be genuinely, wildly different images once actually
   viewed — one a small 256×128 ornate belt/tattoo-style detail, the other
   a large multi-thousand-pixel full skin/face/jewelry texture atlas. Real
   content, not a bug; the size difference (~40KB vs. ~550KB) is just
   real resolution difference.

**Verdict**: no decode bug found. The most likely explanation for "looks
like the same image" is that WoW's own customization-variant textures
*are* often deliberately near-duplicate (subtle skin-tone/hair-color
recolors of the same base image) — that's the actual point of a
color-variant slot — and skimming dozens of small thumbnails in Blender's
material preview panel makes genuinely-different-but-similar images easy
to mistake for literal duplicates at a glance.

**A real, related usability gap found while investigating, not yet
fixed**: `alternate_textures` currently attaches the *entire* undifferentiated
94-file candidate pool to *every* ambiguous material, regardless of which
hardcoded `textureType` that specific slot actually is. A `hair_style`
slot's alternate list includes `skin_color`/`jewelry_color`/`blindfold`
files that could never be a valid answer for it — structurally impossible,
not just unlikely. Filtering the pool per-slot by a cheap keyword
heuristic (matching a candidate's filename against `m2::textureTypeName`'s
own vocabulary, e.g. only offering `hair_style_*`/`hair_color_*` files to
a slot whose `textureType` names hair) would cut real noise, without
crossing into "guessing the one correct answer" — an earlier investigation
this project already did (real texture-directory naming vs.
`M2Texture::type`'s own enum vocabulary, found to be two genuinely
unrelated naming schemes) settled that *picking* between candidates by
keyword would just be a different, equally unfounded guess; *filtering*
which candidates are even offered is a different, much safer claim than
picking a single winner within a category. Not implemented this session —
a real, scoped follow-up if this becomes annoying in practice.
