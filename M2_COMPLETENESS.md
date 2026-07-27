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
| Mesh geometry (positions, indices) | full | native | native — 100% | `src/m2.cpp`/`src/skin.cpp` |
| Normals | full | native | native — 100% | part of `M2Vertex` |
| UV / texture coordinates (both sets) | full | native | native — 100% | `TEXCOORD_0`/`TEXCOORD_1` |
| Tangents | n/a | n/a | n/a | not in the documented base header at all — nothing to parse |
| Vertex skinning (bone weights/indices) | full | native | native — 100% | `JOINTS_0`/`WEIGHTS_0` |
| Skeleton / bone hierarchy | full | native | native — 100% | inline or `.skel`-sourced, same joint hierarchy either way |
| LOD tiers (`.skin` selection, `--lod`) | full | native | native — 100% | `--lod all` exports every tier as its own node sharing one skeleton |
| Per-vertex color (batch tint/fade, constant case) | full | native | native — 100% | not truly per-vertex — per-batch material tint, see Materials section for the animated case |

## Animation

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Animation sequences + per-bone tracks (inline or `.skel`-sourced) | full | native | native — 100% | `resolveVec3TrackSequence`/`resolveQuatTrackSequence` |
| External `.anim` sequences (`AFM2`) | full | native | native — 100% | via `--anim <dir>` + `AFID`/`.skel`'s own `AFID` |
| External `.anim` sequences (`AFSB`, `.skel`-linked) | full | native | native — 100% | byte layout was undocumented anywhere, cracked this session — `SKB1`'s own descriptors point directly into `AFSB`'s payload, no new parser needed (`WIKI_FINDINGS.md` §2's follow-up) |
| Global-sequence bone tracks (independent continuous loops) | full | native | native — 100% | `buildGlobalSequenceAnimations`, one clip per global-sequence index |
| Alias sequences (`flags & 0x40`) | none | none | n/a | wowdev.wiki itself: "I have no clue" where this data lives — an upstream-spec gap, not a husk gap |
| Animated material tint/fade (`M2Color`/`M2TextureWeight`, global-sequence-driven) | deref (animated flag detected) | diagnostic (stderr note only) | native-possible, not done | no core-glTF animation-channel target for a material property exists either way, but husk hasn't even attempted the extras-based keyframe dump that would at least surface the data |

## Materials & textures

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Base material (blend mode, alpha mode, doubleSided) | full | native | native — 100% | `alphaMode`/`doubleSided` |
| Texture references (names/FileDataIDs, `TXID`) | full | native | native — 100% | real embedded `baseColorTexture` via `--textures <dir>` |
| Multi-texture-layer (`textureCount > 1`, 2nd+ layer) | full | extras | extras-capped, permanent | core glTF has no slot for WoW's fixed-function combiner math (`Mod2x`/`Add`/env-map); index arithmetic itself also unverified against a real multi-layer file, `TODO_correctness.md` #5 |
| Texture transform (UV scroll/rotate/scale, animated case) | full | extras | extras-capped, permanent | `KHR_texture_transform` has no animation-channel target — the common case has no representation regardless of effort |
| Texture transform (UV scroll/rotate/scale, constant case) | full | extras | native-possible, unverified | pivot-correction math (texture-center vs. extension's origin) not checked against a real file yet |

## Collision & physics

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Collision box / sphere radius (scalars) | full | diagnostic | n/a — verification-signal only | not an export target; cross-checked against husk's own bind-pose vertex parsing (containment, not tight fit — see `WIKI_FINDINGS.md` §5) by `tests/test_conformance.cpp`, `VERIFICATION_IDEAS.md` Case 3 |
| Collision mesh (positions/indices/face normals) | full | native | native — 100% | a plain unskinned triangle mesh, one more `gltf::NamedMesh` tagged `{"collision": true}` in node `extras` for a renderer/Blender script to filter — per-vertex normals approximated (averaged adjacent face normals) since the source data is per-triangle, not per-vertex; see `VERIFICATION_IDEAS.md` Case 5 |
| `.phys` sidecar content | none (only the `PFID` FileDataID itself is read) | none | n/a — unscoped | nobody has reverse-engineered `.phys`'s own byte layout yet, unlike every other sidecar |

## Interaction points & effects

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Attachments | deref (static fields only) | diagnostic (`husk info`) | node-possible, unclaimed | could become real empty glTF nodes; not attempted |
| Events | deref (static fields only) | diagnostic (`husk info`) | node-possible, unclaimed | same as attachments |
| Lights | deref (static fields only) | diagnostic (`husk info`) | node-possible, unclaimed | color/intensity/attenuation tracks are animated and skipped even at parse time |
| Cameras (`M2Camera`) | descriptor (count only) | diagnostic (count only) | n/a, deprioritized by design | a custom renderer supplies its own camera regardless — see `TODO_correctness.md` #3 |
| Ribbons (`M2Ribbon`) | deref (static fields only) | diagnostic (`husk info`) | native-possible, not done | 6 embedded `M2Track`s (color/alpha/height/texSlot/visibility) + 2 lookup tables still unread |
| Particles (`M2Particle`) | descriptor (count only) | diagnostic (count only) | native-possible, not done | large, version-conditional struct, still entirely unparsed — `TODO_correctness.md` #2 |
| Particle/ribbon side-chunks (`TXAC`/`EXPT`/`RPID`/`GPID`/`PGD1`) | full | diagnostic (`dump-chunks` JSON) | n/a, by design | `dump-chunks` is a deliberately separate JSON output, never a step toward richer glTF (see `DESIGN.md`) |

## Sidecars & lookup tables

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Sidecar FileDataIDs actually resolved to a local file (`SFID`/`TXID`/`AFID`/`BFID`) | full | native (`SFID`/`TXID`/`AFID`) or extras-only (`BFID`, see the `.bone` row below) | native — 100% (`SFID`/`TXID`/`AFID`); extras-only, by design (`BFID`) | `--skin-dir`/`--textures`/`--anim`/`--bones-dir`, local-directory convention only, never CASC |
| Sidecar FileDataIDs surfaced but not resolved (`SKID`/`PFID`) | full (raw ID only) | diagnostic (`husk info`) | n/a — CASC resolution is a hard non-goal | no local-directory convention exists for these two |
| `.bone` correction matrices (`BIDA`/`BOMT`) | full | diagnostic (`dump-chunks`) + inert `extras` (`husk export --bones-dir`) | extras-only, by design — never applied to the render | LOD ruled out as the selector by real data; slot selection likely a client-side customization-choice DB2 lookup husk can't reach — `TODO_correctness.md` #6 |
| Lookup tables (`boneLookup`/`attachmentLookup`/`cameraLookup`/`textureLookup`/`sequenceLookup`) | descriptor | none | n/a, unclaimed | not even printed by `husk info` today — `TODO_correctness.md` #4 |

## Infrastructure (not independently renderable)

| Feature | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| Chunk container / magic detection (`MD20`/`MD21`, tag tracking) | full | diagnostic (`husk info`) | n/a, infrastructure | drives parsing itself; no renderable shape of its own |
| Header / global metadata (name/flags/bounding box/counts) | full | diagnostic (`husk info`) | n/a, infrastructure | same reasoning |
| Submesh/batch hardware bone-limit metadata (culling/sort fields) | descriptor | none | n/a, deliberately bypassed | husk exports full per-vertex global joint indices instead of the engine's per-drawcall bone-limit mechanism this metadata exists for — an intentional substitute, not a gap |
