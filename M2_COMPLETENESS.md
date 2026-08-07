# M2 feature completeness

A granular, M2-only breakdown of "how done is this feature," one row per
concrete M2 data element. This is **not** a replacement for `README.md`'s
format matrix (M2 vs M3 vs WMO vs BLP) — that table answers "does husk
handle this concept at all, in which formats." This file answers a
different question for M2 specifically: for each concept husk *does* touch,
how far does it actually go, and — critically — is the remaining distance
something husk can still close, or a permanent wall built into glTF/Blender
itself.

Like `README.md`/`DESIGN.md`, **this file describes current state and is
kept in sync as things change** — it is not a punch list (that's
`TODO_correctness.md`) and not a historical record (that's git history).
When a row's status changes, edit the row; don't append a note.

## The three axes

**Parse depth** — how much of the M2/`.skin`/`.skel`/`.anim`/`.bone` byte
layout husk actually decodes for this feature:

| Value | Meaning |
|---|---|
| `none` | Not read at all, or detected-and-skipped (e.g. the `0x40` "alias" sequence flag) |
| `descriptor` | Only the `M2Array` count+offset pair is read — no real records |
| `deref` | Real records dereferenced, but only their static (non-`M2Track`) fields |
| `full` | Every field husk needs for the feature, including animated tracks |

**Consumption** — where the parsed data actually goes:

| Value | Meaning |
|---|---|
| `none` | Parsed into memory, then dropped — invisible anywhere, including `husk info` |
| `diagnostic` | Visible via `husk info`/`dump-chunks`; never reaches the exported `.glb` |
| `extras` | Written into the `.glb` as glTF `extras` — present in the file, inert to any renderer that doesn't specifically look for it |
| `native` | A real core-glTF construct (accessor, skin, animation channel, material field) — renders in Blender with zero extra tooling |

**glTF ceiling** — the honest answer to "why isn't this `native`, and can it
ever be":

| Value | Meaning |
|---|---|
| `native — 100%` | Already there for the data currently available |
| `native — gap remains` | `native` is the right target and is reachable; a specific known gap blocks it today |
| `native-possible, not done` | Nothing about glTF/Blender blocks a real translation; husk just hasn't built it yet |
| `node-possible, unclaimed` | Could become a plain glTF node (empty transform, no core semantic) rather than a first-class construct; nobody's built even that |
| `extras-capped, permanent` | No core-glTF or common-Blender-importer slot exists for this concept, full stop — more husk effort cannot close this gap, only extras-tagging can |
| `n/a` | Out of scope by deliberate project decision, an infrastructure concern with no renderable shape, or a genuine upstream-spec unknown (not a husk gap) |

Only `native — 100%` means "done." Everything else is a real, specific
reason it isn't — read the ceiling column before assuming any `⬜`-style row
elsewhere is just unattempted work.

---

## Core geometry & skeleton

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Mesh geometry (positions, indices) | full | native | native — 100% | `src/m2.cpp`/`src/skin.cpp`; a genuinely geometry-less model (0 vertices, real corpus shape for particle/ribbon-only VFX — 3,807 files in a 130k-file corpus sweep) exports skeleton + emitter anchors with zero mesh nodes instead of failing — see `DESIGN.md`'s Key design decisions |
| Normals | full | native | native — 100% | part of `M2Vertex` |
| UV / texture coordinates (both sets) | full | native | native — 100% | `TEXCOORD_0`/`TEXCOORD_1` |
| Tangents | n/a | n/a | n/a | not in the documented base header at all — nothing to parse |
| Vertex skinning (bone weights/indices) | full | native | native — 100% | `JOINTS_0`/`WEIGHTS_0` |
| Skeleton / bone hierarchy | full | native | native — 100% | inline or `.skel`-sourced, same joint hierarchy either way; a multi-root bone forest (35% of a real 130k-file corpus, `tools/find_multiroot_skeletons.py`) gets one synthesized non-joint glTF parent node, never a fake extra joint or a dropped bone — see `DESIGN.md`'s Key design decisions |
| LOD tiers (`.skin` selection, `--lod`) | full | native | native — 100% | `--lod all` exports every tier as its own node sharing one skeleton |
| Per-vertex color (batch tint/fade, constant case) | full | native | native — 100% | not truly per-vertex — per-batch material tint, see Materials section for the animated case |

## Animation

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Animation sequences + per-bone tracks (inline or `.skel`-sourced) | full | native | native — 100% | `resolveVec3TrackSequence`/`resolveQuatTrackSequence`; a real, exact-duplicate keyframe timestamp (an authored "hard cut" pose, 5 real corpus files) is repaired via a 1ms forward nudge rather than rejected — see `DESIGN.md`'s Key design decisions |
| `M2Sequence`'s own metadata (`movespeed`/`frequency`/`replay`/`blendTimeIn`/`blendTimeOut`/`bounds`/`variationNext`/`aliasNext`) | full | extras | extras-capped, permanent | no core-glTF clip field for playback speed/blend timing/replay count/bounding volume exists — attached as `sequence_metadata` extras on each clip (`gltf::Animation::SequenceMetadata`), same "tag it, don't guess at semantics" treatment as `bone_correction_sets`/`ribbon_emitters` |
| External `.anim` sequences (`AFM2`) | full | native | native — 100% | via `--anim <dir>` + `AFID`/`.skel`'s own `AFID` |
| External `.anim` sequences (`AFSB`, `.skel`-linked) | full | native | native — 100% | byte layout was undocumented anywhere, cracked this session — `SKB1`'s own descriptors point directly into `AFSB`'s payload, no new parser needed (`WIKI_FINDINGS/M2/anim.md`'s follow-up) |
| Global-sequence bone tracks (independent continuous loops) | full | native | native — 100% | `buildGlobalSequenceAnimations`, one clip per global-sequence index |
| Alias sequences (`flags & 0x40`) | full | native | native — 100% | `aliasNext` resolved (`WIKI_FINDINGS/M2.md`): a local `sequences` array index, chain-walked to its terminal non-alias sequence, whose keyframe data (inline or external, whichever it uses) is reused for the alias's own clip — a sequence carrying *both* `0x20` (inline) and `0x40` (alias) uses its own inline data, `0x20` winning priority exactly as it did before this existed (real data: 31 of 38 real alias sequences in `bloodelffemale_hd.skel` carry both bits) |
| Animated material tint/fade (`M2Color`/`M2TextureWeight`, per-sequence or global-sequence-driven) | full | extras | extras-capped, permanent | no core-glTF animation-channel target for a material property exists — `resolveAnimatedColorCurve`/`resolveAnimatedFixed16Curve` (`src/cmd_export.cpp`, reusing `resolveVec3TrackSequence`/`resolveRawIntTrackSequence`) resolve the full curve and attach it as `tint_animation`/`fade_animation` material extras (`gltf::Material`) for a custom renderer/Blender script to play back itself |

## Materials & textures

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Base material (blend mode, alpha mode, doubleSided) | full | native | native — 100% | `alphaMode`/`doubleSided` |
| Texture references (names/FileDataIDs, `TXID`) | full | native | native — 100% | real embedded `baseColorTexture` via `--textures <dir>` |
| Hardcoded/replaceable texture slot marker (`M2Texture::type != 0`) | full | extras | extras-capped, permanent | no core-glTF concept for "this slot is resolved by client-side DB2/character-customization data at runtime" — a nonzero `type` (skin/hair/item-tint/...) is attached as `texture_type` material extras (present only when nonzero, matching every other "absence means ordinary" extras convention here) so a missing `baseColorImagePng` reads as "husk can't resolve this locally," not "the `--textures` directory just didn't have the file" |
| Multi-texture-layer (`textureCount > 1`, 2nd+ layer) | full | extras | extras-capped, permanent | core glTF has no slot for WoW's fixed-function combiner math (`Mod2x`/`Add`/env-map); index arithmetic confirmed exact against real data, `WIKI_FINDINGS/M2/skin.md` |
| Texture transform (UV scroll/rotate/scale, animated case) | full | extras | extras-capped, permanent | `KHR_texture_transform` has no animation-channel target — the common case has no representation regardless of effort |
| Texture transform (UV scroll/rotate/scale, constant case) | full | extras | native-possible, unverified | pivot-correction math (texture-center vs. extension's origin) not checked against a real file yet |

## Collision & physics

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Collision box / sphere radius (scalars) | full | diagnostic | n/a — verification-signal only | not an export target; cross-checked against husk's own bind-pose vertex parsing (containment, not tight fit — see `WIKI_FINDINGS/M2.md`) by `tests/test_conformance.cpp` |
| Collision mesh (positions/indices/face normals) | full | native | native — 100% | a plain unskinned triangle mesh, one more `gltf::NamedMesh` tagged `{"collision": true}` in node `extras` for a renderer/Blender script to filter — per-vertex normals approximated (averaged adjacent face normals) since the source data is per-triangle, not per-vertex |
| `.phys` sidecar content | full | extras + diagnostic | extras-capped, permanent | documented on wowdev.wiki (`documentation/wowdev-wiki/md/PHYS.md`), verified against 103 real files (`WIKI_FINDINGS/PHYS.md`) — every body/shape/joint/`PHYV` record parsed (`src/phys.hpp`/`phys.cpp`); `husk export --phys` attaches a minimal per-body placement anchor (`physics_bodies` skin extras, same pattern as ribbon/particle emitters), full records via `husk dump-chunks <file.phys>` — no core-glTF concept for physics simulation input exists, so this is permanently `extras`/diagnostic, not a renderable gap |
| Inline physics (`PFDC`, M2-embedded) | full | diagnostic (`dump-chunks` JSON) | n/a, by design | wowdev.wiki M2#PFDC: byte-for-byte the same `.phys` chunk container (plus up to 6 bytes trailing zero padding) — reuses husk's own `phys::parse`/`writePhysFile` directly, no new parser; husk's own local extraction corpus has zero `PFDC`-bearing files, but that was a local-extraction gap, not a real absence — a live-CASC full-corpus scan found 2,430 real `PFDC` files, and one pulled directly (FileDataID 1003471) decodes cleanly, same shape `WIKI_FINDINGS/PHYS.md` already verified for standalone `.phys`; now covered by a real-data regression test, `tests/test_dump.cpp` |
| Player-housing collision (`PCOL`, War Within 11.1.7+) | full | diagnostic (`dump-chunks` JSON) | n/a, by design | wowdev.wiki M2#PCOL: four independent `(count, offset)` regions (`vertexPositions`/`faceNormals`/`indices`/`flags`), each read via its own offset since the wiki warns regions aren't necessarily contiguous — confirmed on a real file (an 8-byte gap between `faceNormals` and `indices`); verified against all 2,354 real `PCOL`-bearing files in the local corpus (corrected from an earlier scanner bug's false "zero real files," `WIKI_FINDINGS/M2.md`), every region in-bounds and every index in range for that file's own vertex count on all 2,354, `indexCount == faceNormCount * 3` on all 2,354 (triangle triples, one normal per triangle — the same shape M2's own core collision mesh above already has); `flags`' per-record meaning is undocumented on the wiki, exposed raw; same `dump-chunks`-only, no-glTF-slot treatment as `EXP2`/`PFDC`/`DETL` (niche sidecar-shaped data, not core render geometry) |

## Interaction points & effects

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Attachments | deref (static fields only) | native (`husk export`) | native — 100% | real, translation-only child glTF node per entry (`attachment_<id>`), parented under its owning joint, never added to `skin.joints` — see `DESIGN.md`'s Key design decisions |
| Events | deref (static fields only) | native (`husk export`) | native — 100% | same treatment as attachments, node named `event_<identifier>` (not deduplicated — a real M2 can repeat one, e.g. multiple "$CSD" events) |
| Lights | deref (static fields only) | native (`husk export`) | native — 100% | position/joint only, node named `light_<index>`; `type` and every animated field (color/intensity/attenuation/visibility) remain out of scope, no glTF slot attempted for those |
| Per-light shadow-RT scale / diffuse multiplier (`DETL`) | full | diagnostic (`dump-chunks` JSON) | n/a, by design | real 12-byte stride + 16-byte chunk alignment padding, both confirmed against 1,043 real corpus files (`WIKI_FINDINGS/M2.md`) — no core-glTF slot for shadow-render-target scale/diffuse-multiplier data |
| `WFV1`/`WFV2`/`DPIV`/`AFRA` (no wowdev.wiki struct at all) | full — byte-decoded from real files, no wiki basis (`AFRA`/`WFV1`: 1 float + reserved; `DPIV`: real `chunk.size/32` record array, 8 floats each; `WFV2`: flat 16x float array) | diagnostic (`dump-chunks` JSON) | n/a, by design | `WFV1`/`WFV2` are a genuinely thin 2-file, byte-identical sample — flagged tentative, field names deliberately generic; verified against all real corpus hits (`WIKI_FINDINGS/M2.md`'s follow-up) |
| Cameras (`M2Camera`) | descriptor (count only) | diagnostic (count only) | n/a, deprioritized by design | a custom renderer supplies its own camera regardless — see `TODO_correctness.md` #3 |
| Ribbons (`M2Ribbon`) | full — all static fields + 6 `M2Track`s resolved (curves) + 2 lookup tables | `husk info` (summary) + `dump-chunks` JSON (full, every resolved curve) + inert `extras` (`husk export`, minimal id/bone/position anchor) | extras-only, by design — no native glTF emitter/trail primitive exists | real-data-verified against Ashbringer's 3 ribbons — see `WIKI_FINDINGS.md` |
| Particles (`M2Particle`) | full for version ≥ `kMinVerifiedParticleVersion` (272, Cataclysm) — all static fields + FBlock curves (flat) + `M2Track<float>` curves (per-sequence/global-sequence) resolved; count-only + loud warning below that version | `husk info` (summary) + `dump-chunks` JSON (full, every resolved curve) + inert `extras` (`husk export`, minimal id/bone/position anchor) | extras-only, by design — no native glTF emitter/particle-system primitive exists | real-data-verified against a real weapon's 64 particle emitters (decoded colors form a real fire/ember gradient, alpha/scale curves are clean envelopes) — see `WIKI_FINDINGS.md`; pre-Cata shapes real but unverified, not implemented |
| Particle/ribbon side-chunks (`TXAC`/`EXPT`/`RPID`/`GPID`/`PGD1`/`EXP2`) | full | diagnostic (`dump-chunks` JSON) | n/a, by design | distinct from the `M2Ribbon`/`M2Particle` records themselves (above) — `dump-chunks` is a deliberately separate JSON output, never a step toward richer glTF (see `DESIGN.md`). `EXP2` (M2ExtendedParticle: `zSource`/`colorMult`/`alphaMult` — the same three fields `EXPT` carries — plus a new `alphaCutoff` `M2PartTrack<fixed16>` curve; per the wiki, the client reconstructs `EXP2` from `EXPT` when only the latter exists, so `EXP2` is `EXPT`'s superset, not an unrelated record) — husk's own local extraction corpus has zero `EXP2`-bearing files, but that was a local-extraction gap, not a real absence: a live-CASC full-corpus scan found 17,065 real `EXP2` files, and two pulled directly (FileDataIDs 126382/1003471) decode cleanly with no changes needed, one showing a real monotonic 3-keyframe `alphaCutoff` curve; now covered by real-data regression tests, `tests/test_dump.cpp` |

## Sidecars & lookup tables

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Sidecar FileDataIDs actually resolved to a local file (`SFID`/`TXID`/`AFID`/`BFID`) | full | native (`SFID`/`TXID`/`AFID`) or extras-only (`BFID`, see the `.bone` row below) | native — 100% (`SFID`/`TXID`/`AFID`); extras-only, by design (`BFID`) | `--skin-dir`/`--textures`/`--anim`/`--bones-dir`, local-directory convention only, never CASC |
| Sidecar FileDataIDs surfaced but not resolved (`SKID`/`PFID`) | full (raw ID only) | diagnostic (`husk info`) | n/a — CASC resolution is a hard non-goal | no local-directory convention exists for these two |
| `.bone` correction matrices (`BIDA`/`BOMT`) | full | diagnostic (`dump-chunks`) + inert `extras` (`husk export --bones-dir`) | extras-only, by design — never applied to the render | LOD ruled out as the selector by real data; slot selection likely a client-side customization-choice DB2 lookup husk can't reach — `TODO_correctness.md` #6 |
| Lookup tables (`boneLookup`/`attachmentLookup`/`cameraLookup`/`textureLookup`/`sequenceLookup`) | descriptor | none | n/a, unclaimed | not even printed by `husk info` today — `TODO_correctness.md` #2 |

## Infrastructure (not independently renderable)

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Chunk container / magic detection (`MD20`/`MD21`, tag tracking) | full | diagnostic (`husk info`) | n/a, infrastructure | drives parsing itself; no renderable shape of its own |
| Header / global metadata (name/flags/bounding box/counts) | full — every field the wowdev.wiki header struct documents, including `global_flags` decoded into named bits (`m2::globalFlagNames`) and the conditional `textureCombinerCombos` array | diagnostic (`husk info`) | n/a, infrastructure | same reasoning; `textureCombinerCombos` has zero real corpus hits (130,576-file scan), so its own layout is unverified against real bytes even though the parse itself is a plain, unambiguous `M2Array<uint16_t>` |
| Submesh/batch hardware bone-limit metadata (culling/sort fields) | descriptor | none | n/a, deliberately bypassed | husk exports full per-vertex global joint indices instead of the engine's per-drawcall bone-limit mechanism this metadata exists for — an intentional substitute, not a gap |
