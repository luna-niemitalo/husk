# EYES_ON_FINDINGS.md

Findings from a real interactive Blender inspection of a `husk export` output
(post-`TRANSFORM_TRIAGE.md` orientation fix). Mesh completeness and
orientation are both confirmed good now. Four items raised; each
investigated against the current source, not guessed at.

## 1. Texture conversion is a manual second step — usability gap, not a bug

**Symptom**: even given a correctly-populated CASC-exported texture
directory, `husk export` does not produce usable textures on its own —
`husk-blp` still has to be run by hand afterward.

**Grounding**: this is the documented pipeline shape, not an oversight.
`DESIGN.md:528-533` states BLP/texture conversion is a separate Python
process *permanently* — husk reads a `.png` `husk-blp` already wrote, it
never invokes `husk-blp` or links against Pillow (DXT/BC block decode needs
Pillow; `.blp` is a listed Non-goal, `DESIGN.md:1113-1114`). `--textures`'s
own help text (`src/cmd_export.cpp:714-715`) says the directory must hold
*already-converted* `<FileDataID>.png` files; texture resolution
(`src/export_materials.cpp:411-484`) only ever looks for `.png`, never
constructs a `.blp` path. `README.md:372-395` spells out the two-step
workflow explicitly.

**Verdict**: working as designed at the process-boundary level, but the
*workflow* around it has no glue — a CASC export directory is 100% `.blp`,
and nothing in this repo bridges "I have a CASC export" to "husk can read
textures from it" in one step. Every other sidecar format (`.skin`,
`.skel`, `.anim`, `.bone`, `.phys`) that a CASC exporter would also produce
gets located automatically by `--*-dir` flags; textures are the one format
that doesn't, because the conversion itself (not just the *lookup*) has to
happen first and lives in a different tool/language entirely.

**Action** (not yet implemented, no code changed this session): a thin
wrapper script — bulk-run `husk-blp` over every `.blp` under a CASC export
tree into a flat `<FileDataID>.png` pool, then hand that pool to
`--textures` — would close the gap without violating the
husk/`husk-blp` process boundary. Belongs in `blp/` or a top-level
`scripts/` dir, not inside `husk` itself.

## 2. Unnamed bones (`bone_29`) — real source-data limitation, already partially solved

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
real `bloodelffemale.m2` fixture (`BLENDER_EXPORT_TODO.md:626-648`): 14 of
119 bones get a real name, the rest fall back — "a real fact about this
fixture, not a bug in the lookup."

**Verdict**: not a gap to close in husk. The wrist specifically not having
a key-bone slot is upstream WoW rig data, not a husk omission — Blizzard's
own key-bone table has no "Wrist" entry at all (it jumps hand → forearm →
shoulder). `bone_<index>` is the correct, already-intentional fallback.

## 3. Root bone (bone 0) holding most of the mesh's weight — real M2 data, faithfully passed through

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

## 4. Material naming granularity — usability gap, real duplicates confirmed

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
