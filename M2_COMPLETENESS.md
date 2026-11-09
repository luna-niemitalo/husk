# M2 feature completeness

A granular, M2-only breakdown of "how done is this feature," one row per
concrete M2 data element. This is **not** a replacement for `README.md`'s
format matrix (M2 vs M3 vs WMO vs BLP) — that table answers "does husk
handle this concept at all, in which formats." This file answers, for M2
specifically: for each concept husk *does* touch, how far does it go.

Like `README.md`/`DESIGN.md`, **this file describes current state and is
kept in sync as things change** — it is not a punch list (open items live
across the individual `TODO/*.md` files, see `TODO/README.md`'s index) and
not a historical record (that's git history).
When a row's status changes, edit the row; don't append a note.

## The two axes

**Parse depth** — how much of the M2/`.skin`/`.skel`/`.anim`/`.bone` byte
layout husk actually decodes for this feature:

| Value | Meaning |
|---|---|
| `none` | Not read at all, or detected-and-skipped |
| `descriptor` | Only the `M2Array` count+offset pair is read — no real records |
| `deref` | Real records dereferenced, but only their static (non-`M2Track`) fields |
| `full` | Every field husk needs for the feature, including animated tracks |

**Consumption** — where the parsed data goes:

| Value | Meaning |
|---|---|
| `none` | Parsed into memory, then dropped |
| `diagnostic` | `husk info`/`dump-chunks` only |
| `bundle` | Written into the model bundle (`husk export`, `REFACTOR/BUNDLE_FORMAT.md`) as data; never applied |
| `bundle + glb` | Also in `export --glb`'s lean glTF projection (mesh, skin, animation, base-colour materials — nothing without a core glTF slot) |

---

## Core geometry & skeleton

| Feature | Parse | Consumption | Note |
|---|---|---|---|
| Mesh geometry (positions, indices) | full | bundle + glb | `src/m2.cpp`/`src/skin.cpp`; a geometry-less model (particle/ribbon-only VFX, 3,807 files in a 130k-file sweep) exports its skeleton and effects with no triangles (`DESIGN.md`) |
| Normals | full | bundle + glb | part of `M2Vertex` |
| UV / texture coordinates (both sets) | full | bundle + glb | |
| Tangents | n/a | n/a | not in the documented base header at all — nothing to parse |
| Vertex skinning (bone weights/indices) | full | bundle + glb | |
| Skeleton / bone hierarchy | full | bundle + glb | inline or `.skel`-sourced; a multi-root forest (35% of the corpus, `tools/find_multiroot_skeletons.py`) gets one non-joint parent node in the glb (`DESIGN.md`) |
| LOD tiers (`.skin` per `SFID` entry) | full | bundle | every level in `resources.geometry.lod` |
| Per-batch tint/fade, constant case | full | bundle | one-key step curves on the material layer |

## Animation

| Feature | Parse | Consumption | Note |
|---|---|---|---|
| Animation sequences + per-bone tracks (inline or `.skel`-sourced) | full | bundle + glb | `resolveVec3TrackSequence`/`resolveQuatTrackSequence`; an exact-duplicate keyframe timestamp (a "hard cut", 5 corpus files) is repaired by a 1 ms nudge (`DESIGN.md`) |
| `M2Sequence` metadata | full | bundle | each clip's `animation` (AnimationData row, named when the DB2 has `Name`), `variation`, `duration`, `move_speed`, `frequency`, `blend_in`/`blend_out`, `source{flags, variation_next}`. `bounds`/`replay` are not written |
| External `.anim` (`AFM2`) | full | bundle + glb | via `--anim <dir>` + the model's or `.skel`'s `AFID` |
| External `.anim` (`AFSB`, `.skel`-linked) | full | bundle + glb | `SKB1`'s descriptors point straight into `AFSB`'s payload (`WIKI_FINDINGS/M2/anim.md`) |
| Global-sequence bone tracks | full | bundle + glb | their own global loop, not folded into sequence clips |
| Alias sequences (`flags & 0x40`) | full | bundle + glb | chain-walked through `aliasNext` to the terminal sequence; `0x20` (inline) wins over `0x40` (31 of 38 aliases in `bloodelffemale_hd.skel` carry both) |
| Animated material tint/fade (`M2Color`/`M2TextureWeight`, per-sequence or global) | full | bundle | material layer `tint`/`alpha_fade` curves |

## Materials & textures

| Feature | Parse | Consumption | Note |
|---|---|---|---|
| Base material (blend mode, render flags) | full | bundle + glb | `framebuffer_blend`, `unlit`, `unfogged`, ...; the glb maps blend to `alphaMode` |
| Texture references (FileDataIDs, `TXID`) | full | bundle + glb | an unresolved texture keeps its FileDataID; a literal hit takes the listfile name |
| Replaceable texture slot (`M2Texture::type != 0`) | full | bundle | `definition.texture_slots`; each layer names its `slot`. Creature models also get slot texture options, particle colour options and `selection.presets` from CreatureDisplayInfo (`TODO/CREATURE_OPTIONS_TODO.md`); characters resolve slot textures through the DB2 character tier |
| Multi-texture layers (`textureCount > 1`) | full | bundle | `layers[]`; index arithmetic confirmed against real data (`WIKI_FINDINGS/M2/skin.md`) |
| Texture transform (UV scroll/rotate/scale), animated or constant | full | bundle | layer curves; a constant transform is a one-key step curve. Fixture: `unk_exp11_7037014.m2` (`kTextureTransformTranslationM2`) |

## Collision & physics

| Feature | Parse | Consumption | Note |
|---|---|---|---|
| Collision box / sphere radius (scalars) | full | diagnostic | cross-checked against bind-pose vertices (containment, `WIKI_FINDINGS/M2.md`) by `tests/test_conformance.cpp` |
| Collision mesh (positions/indices/face normals) | full | bundle | `resources.geometry.collision`, always written; normals per triangle, as in the source |
| `.phys` sidecar | full | bundle + diagnostic | `resources.physics` (bodies, shapes, joints); full records via `dump-chunks <file.phys>`. Verified against 103 files (`WIKI_FINDINGS/PHYS.md`) |
| Inline physics (`PFDC`) | full | diagnostic | the `.phys` container inline (+ up to 6 bytes zero padding); reuses `phys::parse`. 2,430 live files; FileDataID 1003471 regression-tested (`tests/test_dump.cpp`) |
| Player-housing collision (`PCOL`, 11.1.7+) | full | diagnostic | four independent `(count, offset)` regions; verified on all 2,354 local `PCOL` files; `flags` is a per-triangle bitmask, bit meanings unknown (`WIKI_FINDINGS/M2.md`) |

## Interaction points & effects

| Feature | Parse | Consumption | Note |
|---|---|---|---|
| Attachments | full | bundle | `resources.scene.attachments`, incl. `.skel` `SKA1`. `animate_attached` empty on all 3 fixtures checked (23 attachments) — matches the wiki's "default true" note |
| Events | deref (static fields) | bundle | `resources.scene.events`; identifiers repeat legally (one weapon has six `$CSD`) |
| Lights | full | bundle | `resources.scene.lights`: type and all 7 `M2Track` fields; checked on `ui_mainmenu_pandaria.m2` (2 animated lights) |
| Per-light shadow-RT scale / diffuse multiplier (`DETL`) | full | diagnostic | 12-byte stride + 16-byte alignment, confirmed on 1,043 files (`WIKI_FINDINGS/M2.md`) |
| `WFV1`/`WFV2`/`DPIV`/`AFRA` (no wiki struct) | full — byte-decoded from real files | diagnostic | `WFV1`/`WFV2` rest on 2 byte-identical files: tentative, generic field names |
| Cameras (`M2Camera`) | descriptor (count only) | diagnostic | deprioritized: a renderer supplies its own camera |
| Ribbons (`M2Ribbon`) | full — static fields, 6 `M2Track`s, 2 lookup tables | bundle + diagnostic | bone-anchored placeholder effects in the model's inline `effects` set, every field and curve as style, textures resolved (`REFACTOR/EFFECTS.md`); full records in `dump-chunks`. Verified on Ashbringer's 3 ribbons |
| Particles (`M2Particle`) | full for version ≥ 272 (`kMinVerifiedParticleVersion`); count-only + warning below | bundle + diagnostic | as ribbons. Verified on a weapon's 64 emitters; pre-Cata shapes unimplemented |
| Particle/ribbon side-chunks (`TXAC`/`EXPT`/`RPID`/`GPID`/`PGD1`/`EXP2`) | full | diagnostic | `EXP2` is `EXPT`'s superset plus an `alphaCutoff` curve; 17,065 live files, FileDataIDs 126382/1003471 regression-tested (`tests/test_dump.cpp`) |

## Sidecars & lookup tables

| Feature | Parse | Consumption | Note |
|---|---|---|---|
| Sidecar FileDataIDs resolved (`SFID`/`TXID`/`AFID`/`BFID`/`SKID`) | full | bundle | found by FileDataID under the given directories, then listfile and store `_by_fdid/`; `.skel` also by same basename (`DESIGN.md` "Three-state resolution") |
| `PFID` | full (raw ID) | diagnostic | not resolved: the `.phys` is `--phys` or the same-basename file |
| `.bone` correction matrices (`BIDA`/`BOMT`) | full | bundle + diagnostic | `resources.rig.correction_sets`; a customization choice names its set. Never applied: semantics unverified (`TODO/BONE_CORRECTION_APPLICATION_TODO.md`); LOD ruled out as the selector |
| Lookup tables (`boneLookup`/`attachmentLookup`/`cameraLookup`/`textureLookup`/`sequenceLookup`) | deref | diagnostic | `husk info` prints all five with names where the id has one; `sequence_lookup` as its hash-bucket shape. Not needed for export: full per-vertex joint indices and resolved textures replace them |

## Infrastructure (not independently renderable)

| Feature | Parse | Consumption | Note |
|---|---|---|---|
| Chunk container / magic detection (`MD20`/`MD21`, tag tracking) | full | diagnostic | drives parsing itself |
| Header / global metadata | full — every documented field, `global_flags` named bits, conditional `textureCombinerCombos` | diagnostic | `textureCombinerCombos` has zero corpus hits (130,576 files): layout unverified |
| Submesh/batch hardware bone-limit metadata | descriptor | none | deliberately bypassed: full per-vertex global joint indices instead |
