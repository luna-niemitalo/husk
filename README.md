# husk

A CLI for converting World of Warcraft's proprietary model/world formats
(M2, M3, WMO) to common ones. This is the very first slice: reading an M2
file's header and printing what's in it. No conversion yet.

## Why test-first

WoW's file formats move under this tool constantly — new chunk types, new
fields, occasional outright format changes (M2 → M3). The tests in
`tests/` are written directly from [wowdev.wiki's M2 page](https://wowdev.wiki/M2)
(offsets, chunk semantics, version table — transcribed independently of
`src/`, not copied from it), not from the implementation. When the format
moves and something in `src/` no longer matches, the goal is for a test to
fail loudly and point at exactly which field/offset broke, rather than
`husk` silently misreading a file. See the comments at the top of
`tests/test_m2.cpp` and `tests/test_chunk.cpp` for the exact spec citations.

## Building

From this directory (`tools/husk/`), inside its own Nix dev shell:

```
direnv allow          # first time only, or: nix develop ./nix -c bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

The binary lands at `build/husk`.

`blp/` (texture conversion, see Usage below) is a separate Python
subproject with its own setup (`cd blp && uv sync`) -- `uv` is provided by
the same dev shell, but it isn't part of the CMake build above.

## Usage

```
husk info <file.m2>
husk export <file.m2> <file.skin>|auto <output.glb> [file.skel]
                       [--textures <dir>] [--skin-dir <dir>]
```

`info` parses the header and prints: whether the file is pre-Legion (flat
`MD20`) or Legion+ (chunked, `MD21`-wrapped), the version and its
best-guess expansion label, the model's internal name, and record counts
(sequences, bones, vertices, textures, materials) read out of the header's
`M2Array` fields. If a model has zero inline bones *and* an `SKID` chunk
(its skeleton lives in an external `.skel` file instead, see below), `info`
says so explicitly rather than leaving "bones: 0" to be misread as "no
skeleton". It also surfaces the model's `SFID` (skin FileDataIDs, see
`export`'s `auto` below), `LDV1` (LOD count), `BFID` (`.bone` FileDataIDs),
and `AFID` (`.anim` FileDataIDs) chunks when present -- the last two are
surfaced but not yet resolved to actual animation-track content for
external-`.anim`-file sequences (roadmap stage 6). It also prints record
counts for `attachments`/`events`/`lights`/`cameras`/`ribbon_emitters`/
`particle_emitters` -- the header's `(count, offset)` `M2Array` descriptors
are read, but the records themselves aren't dereferenced yet (see the
format matrix). For a chunked file it also lists every top-level chunk tag
found, and separately flags (only when it actually happens) any tag that
isn't even in husk's known-M2-chunk-tag list -- see the Design notes below
for why this format needs that.

`export` resolves the M2's `vertices` array to actual `M2Vertex` records
(position, normal, both UV sets, plus the raw bone weights/indices) and the
given `.skin` file's two-level triangle-index lookup (see `src/skin.hpp`),
converts WoW's Z-up coordinates to glTF's Y-up, and writes a glTF binary --
one primitive per `.skin` batch (see `src/skin.hpp`'s `Batch`), each with a
material carrying the right `alphaMode`/`doubleSided` translated from WoW's
blend mode/render flags, plus a static (non-animated, see below)
`baseColorFactor` tint/fade resolved from the batch's `M2Color`/
`M2TextureWeight` references. Both of the M2's UV sets are exported
(`TEXCOORD_0`/`TEXCOORD_1`), and a material's `baseColorTexture` samples
whichever one the batch's `textureCoordComboIndex` actually points at
(pre-Cataclysm models only -- see `src/cmd_export.cpp`'s
`M2MaterialInputs`). If the M2 has bones -- inline, or in the optional
4th-argument `.skel` file for models that keep them there instead (see
`src/skel.hpp`) -- they're also resolved into a bind-pose glTF skin
(`JOINTS_0`/`WEIGHTS_0`, inverse bind matrices, a joint-node hierarchy).
*Inline* bones (not a `.skel` file, see the design notes below for why)
additionally get real glTF `animation` clips: one per `M2Sequence` whose
keyframe data lives in this M2 rather than an external `.anim` file
(`flags & 0x20`), covering every bone with real translation/rotation/scale
keyframes for that sequence. That covers [roadmap stages
1 through 6](#roadmap-modern-m2--blender-via-gltf) below.

husk doesn't resolve the `.skin`/`.skel` filenames itself (no CASC/listfile
access) -- pass the path to whichever `.skin` matches the M2's LOD you want
(e.g. `bloodelffemale.m2` pairs with `bloodelffemale00.skin`, its LOD0 --
not an `_hd`-suffixed file, which belongs to a separate, much higher-poly
HD-variant M2), *or* pass the literal word `auto` instead of a path, plus
`--skin-dir <dir>`: husk reads the M2's own `SFID` chunk (its skin
FileDataIDs) and looks for `<dir>/<FileDataID>.skin` for entry 0 -- always
"the main skin aka lod0" per wowdev.wiki, the highest-detail LOD, matching
the policy roadmap stage 7 already settled on. This is the same
local-directory-plus-FileDataID-naming convention `--textures` uses below,
not CASC/listfile resolution -- husk never looks anywhere but that one
directory.

Materials get real `baseColorTexture` images, not just metadata, when
`--textures <dir>` points at a directory of PNGs already converted via
`husk-blp` (see below) and named `<FileDataID>.png` -- husk reads the
FileDataID itself off the M2's `TXID` chunk, it just doesn't go looking for
the matching BLP/PNG file on its own (no CASC/listfile access, same
non-goal as `.skin`/`.skel` resolution above). Without `--textures`,
materials still carry the correct blend mode, culling, and color tint/fade,
they just render as a flat tinted surface instead of showing the actual
WoW texture.

Textures are a separate tool, not a `husk` subcommand -- `blp/` is a small
uv-managed Python package (see [roadmap stage
4](#roadmap-modern-m2--blender-via-gltf) below for why):

```
cd blp && uv sync
uv run husk-blp <file.blp> <output.png> [--mip N]
```

Converts a BLP2 texture to PNG (mip level 0, full resolution, by default).
Supports palettized, DXT1, DXT5, and uncompressed-BGRA content; DXT3 and
JPEG content aren't implemented yet (see the format matrix).

husk never touches CASC storage itself and doesn't know or care how you got
the file — get a real `.m2` out of a WoW install first with a separate
extraction tool, e.g. [`casc-tool`](https://github.com/luna-niemitalo/casc-tool)
(a standalone CASC browser/extractor CLI, no relation beyond "also reads
WoW files"):

```
casc-tool extract --storage <wow-install> --listfile <listfile.csv> \
  character/bloodelf/female/bloodelffemale.m2 /tmp/bloodelffemale.m2
husk info /tmp/bloodelffemale.m2
```

Verified against the real, live game install: `character/bloodelf/female/bloodelffemale.m2`
(2,377,292 bytes) parses as Legion+ chunked, version 274, internal name
`BloodElfFemale`, 339 sequences / 119 bones / 8061 vertices / 9 textures /
8 materials, and a bounding box consistent with a humanoid character model.
`husk export bloodelffemale.m2 bloodelffemale00.skin out.glb` resolves all
8061 vertices and 10,458 triangles from that same pair, plus all 119 bones
into a bind-pose glTF skin, and writes a ~570 KiB `.glb` -- the .skin's 70
batches become 70 glTF primitives/materials, splitting into a plausible
`alphaMode` mix (36 `OPAQUE` -- skin/tabard, 32 `MASK` -- hair, 2 `BLEND` --
the eye-glow effect layer) with `doubleSided` correctly following each
batch's own material flags (41 of the 70, hand-cross-checked against the
raw `M2Material` flags/`M2Batch.materialIndex` pairing, not just "some
number came out non-zero"). Passing `--textures <dir>`, where `<dir>` holds
this model's two `TXID`-resolved textures pre-converted via `husk-blp` and
named by FileDataID (`1034713.png`, `220043.png` -- the eye-glow effect
layer's two color variants), embeds them as real, tinygltf-decodable
images -- confirmed by round-tripping the output through tinygltf's own
loader (`tests/test_integration.cpp`), not just "the byte count went up."
Passing `auto` + `--skin-dir <dir>` (where `<dir>` holds `469824.skin`,
this model's own `bloodelffemale00.skin` renamed to its FileDataID) resolves
the identical LOD0 skin via the M2's own `SFID` chunk -- confirmed to pick
FileDataID 469824 (`SFID` entry 0) specifically, not just "some" entry, by
deliberately listing a second, nonexistent FileDataID first in a synthetic
fixture (`tests/test_cli.cpp`) and confirming that one is never touched.
`M2Color`/`M2TextureWeight` resolution turned up a real bug during
development, not just a hypothetical one worth guarding against: this
model's skin material's alpha track has 339 per-sequence sub-arrays (one
per `M2Sequence`), and reading element `[0][0]` unconditionally -- the
initial, wrong implementation -- happened to read sequence 0's alpha
keyframe, `0` (fully transparent), which would have made the entire model
invisible on export. The fix (only resolve a track when it has exactly one
sub-array with exactly one keyframe -- see `src/m2.cpp`'s
`constantTrackValueOffset`) was verified by hand-computing the expected
`baseColorFactor` for every batch from the raw file bytes and confirming
the actual export matched exactly, including that this specific alpha
track correctly falls back to its default (opaque) rather than the wrong
value -- `tests/test_m2.cpp` carries this as a named regression test.
`husk export` on that same pair also produces 256 real glTF animation
clips out of the model's 339 `M2Sequence` entries (282 flagged `stored
inline`, 256 of those actually carrying non-empty per-bone keyframe data
for this particular model) -- every one of the 73,465 resulting rotation
keyframes checked to be finite and unit-norm (`tests/test_integration.cpp`).
Getting `M2Sequence`'s own record size right was itself a real-data catch:
a literal reading of the wiki's struct listing gives 36 bytes, but decoding
every one of the 339 records at that stride produces plausible-looking
data for roughly every other entry and outright garbage (`variationIndex`
in the tens of thousands, multi-gigasecond `duration` values) for the
rest -- a 64-byte stride (accounting for an `M2Bounds bounds` field the
wiki lists with no offset comment, easy to misread as a stale annotation
rather than a real 28-byte gap) decodes all 339 cleanly. See
`src/m2.hpp`'s `Sequence` doc comment and the Design notes below. The
Z-up→Y-up conversion for bone rotation/scale keyframes (`gltf::Quat`
component permutation, `src/cmd_export.cpp`'s `toGltf(m2::Quat)`/
`toGltfScale`) was derived from general change-of-basis principles and
checked numerically against several test rotations (see the Design notes)
rather than taken from an explicit wowdev.wiki formula -- no formula for
this specific step is documented there. **Not yet verified**: actually
watching one of these animations play back correctly in Blender (limb
pivots, timing) -- the checks above prove the data is well-formed and
internally consistent (unit quaternions, sane durations, correct joint
targeting), not that a specific animation *looks* right.
Also stress-tested against the same character's HD variant,
`bloodelffemale_hd.m2` (195,498 vertices!) + its matching
`bloodelffemale_hd00.skin`, producing a ~6.8 MB `.glb` with 45,418
triangles -- exercises the same code path at ~24x the vertex count. That HD
variant's own inline `bones` array genuinely is empty, but -- unlike what
an earlier draft of this README claimed -- that does *not* mean it has no
skeleton: it points an `SKID` chunk at a separate 23.4 MB file,
`bloodelffemale_hd.skel`, instead (roadmap stage 3). `husk export
bloodelffemale_hd.m2 bloodelffemale_hd00.skin out.glb
bloodelffemale_hd.skel` resolves all 245 bones out of that `.skel` file in
well under a second. Passing the M2 without that 4th argument still works
and correctly falls back to an unskinned mesh -- so both the skinned and
unskinned paths are exercised at this model's scale, not just the happy
path with a small model.
That HD pairing is also a real example of the model/`.skin`-mismatch check
earning its keep: `bloodelffemale_hd00.skin`'s `vertices` lookup table
references M2 global vertex indices up to 32938, which only makes sense
against `bloodelffemale_hd.m2`'s 195,498 vertices, not the base
`bloodelffemale.m2` (8061 vertices) -- pairing it with the wrong model is
exactly the failure mode `cmd_export.cpp`'s bounds check exists to catch
loudly instead of silently misreading. Verified so far: the glTF binary
framing round-trips through tinygltf's own loader intact
(`tests/test_gltf.cpp`) and file sizes add up correctly for both real
models; **not yet verified**: actually opening either output in Blender
(no Blender available in the environment
this was built in) -- do that before trusting this is a real, working mesh
end to end.

## Format support matrix (M2 / M3 / WMO / BLP)

The single source of truth for "does husk handle X yet." Rows are grouped
by feature area, not by chunk tag — WMO alone has ~70 documented chunk
tags across its root and group files, most of them small variants of a
handful of real features, so tracking by chunk would be noise. Every chunk
tag this matrix is built from is still named in the table/footnotes so
nothing is silently dropped, just grouped sensibly.

Built directly from wowdev.wiki (M2, M3, WMO, BLP pages, fetched
2026-07-24).

**Legend:**

| Symbol | Meaning |
|---|---|
| ✅ | Read + write implemented |
| 📖 | Read implemented, write not started |
| 🚧 | Partially read (some fields/counts only, not full contents) — current MVP state |
| ⬜ | Not started |
| ⬛ | N/A — this concept doesn't exist in this format |
| ❔ | Blocked — not fully documented upstream as of the 2026-07-24 fetch, not just untackled by us |

| Feature | M2 | M3 | WMO | BLP |
|---|---|---|---|---|
| Chunk container / magic detection | 🚧 `MD20`/`MD21` detected, generic (tag-agnostic) chunk reader in `src/chunk.*`; every top-level chunk tag found is tracked (`Header::chunkTags`) and cross-checked against a wiki-sourced known-tag list (`husk info`'s `documentedM2ChunkTags`), flagging anything husk has never seen — see the Design notes below for why this format needs that | ⬜ (chunked like WMO, 16-byte chunk header + property fields) | ⬜ (`MOMO` wrapper, Legion+) | ⬛ flat header, not chunked |
| Header / global metadata | 🚧 `husk info` reads magic/version/name/flags/bounding boxes/array counts | ⬜ `M3DT` (376 bytes: flags, 2 bounding boxes, particle count) | ⬜ `MOHD` (counts, ambient color, WMOID, bounding box, flags) | ⬜ 148-byte header + 1024-byte palette/JPEG region |
| Skeleton / bone hierarchy | 📖 `bones` resolved to `key_bone_id`/`flags`/`parent_bone`/`pivot` (`src/m2.cpp`'s `parseBones`) whether inline in the M2 or, via `SKID` → `.skel` → `SKB1` (`src/skel.cpp`, `husk export`'s optional 4th argument), in an external file — both feed the same bind-pose glTF joint hierarchy. The three embedded `M2Track` animation blocks per bone are skipped either way, not parsed (stage 6); `SKB1`'s own `key_bone_lookup` field, and every other `.skel` chunk (`SKL1`/`SKA1`/`SKS1`/`SKPD`), are unread too — none needed for a bind-pose skeleton. `husk info` flags when a model needs this (0 inline bones + an `SKID` chunk present) instead of silently reading as bone-less. | ❔ no joint-hierarchy chunk documented — only per-vertex weights/bind-poses exist (`VWTS`/`VIBP`) | ⬛ no skeleton | ⬛ |
| Vertex skinning (bone weights/indices) | 📖 read as part of `M2Vertex` (`bone_weights[4]`/`bone_indices[4]`), wired into a glTF skin (`JOINTS_0`/`WEIGHTS_0`) via `husk export` | ⬜ `VWTS` (weights), `VIBP` (inverse bind poses) | ⬛ | ⬛ |
| Mesh geometry (positions, indices) | 📖 `vertices` array resolved to real `M2Vertex` records (`src/m2.cpp`); triangle indices resolved via one explicitly-given `.skin` file (`src/skin.cpp`); exported to glTF via `husk export` | ⬜ `VPOS`/`VINX`/`VGEO`+`Geoset`/`LODS`/`RBAT` | ⬜ `MOVT`/`MOVI`/`MOVX`, `MOBA` batches, `MORI`/`MORB` triangle-strip variants | ⬛ |
| Normals | 📖 part of `M2Vertex`, resolved | ⬜ `VNML` | ⬜ `MONR` | ⬛ |
| UV / texture coordinates | 📖 both `tex_coords[2]` sets resolved and both exported to glTF (`TEXCOORD_0`/`TEXCOORD_1`); a material's `baseColorTexture` samples whichever set the batch's `textureCoordComboIndex` selects (pre-Cataclysm models only — see `src/cmd_export.cpp`) | ⬜ `VUV0`–`VUV5` (up to 6 sets) | ⬜ `MOTV` | ⬛ |
| Tangents | ❔ not in the documented base header — appears to be runtime-computed, not stored | ⬜ `VTAN` | ⬜ `MOTA` (often auto-generated client-side for shaders 10/14) | ⬛ |
| Per-vertex colors | 📖 not truly per-vertex (M2's `colors`/`textureWeights` are per-*batch* material tint/fade, not per-vertex mesh color — see `src/m2.cpp`'s `Color`/`TextureWeight`); resolved into glTF `baseColorFactor` by `husk export` as a *static* approximation (only when the underlying `M2Track` is unambiguously constant — real keyframe animation is stage 6, see the Design notes below) | ⬜ `VCL0`/`VCL1` | ⬜ `MOCV`/`MOC2` | ⬛ |
| LOD / mesh views | 🚧 one `.skin` file's `vertices`/`indices` lookup tables, plus `submeshes`/`batches` (material/texture linkage per submesh, `src/skin.cpp`'s `Submesh`/`Batch`) read directly; `.skin` filename can be given explicitly, or auto-selected via the M2's own `SFID` chunk + `husk export auto --skin-dir <dir>` (always picks the highest-detail LOD, see Usage) — `LDV1` LOD-count metadata is surfaced (`husk info`) but not otherwise consumed | ⬛ `LODS` folds LOD into the one file, no sidecar | ⬛ (`GFID`'s `Flag_Lod` is a different, coarser concept — tracked under World/group structure) | ⬜ mip pyramid — tracked under Texture pixel data below, not here |
| Collision / physics | 🚧 `bounding_box`/`collision_box` fields read; `.phys` sidecar (`PFID`) untouched | ⬜ `M3CL` collision mesh (`CPOS`/`CNML`/`CINX`) | ⬜ `MOBN`/`MOBR` BSP tree, `MCVP` convex volumes, `MOPL` terrain-cutting planes | ⬛ |
| Materials | 📖 `materials` array (`flags`/`blending_mode`, `src/m2.cpp`'s `parseMaterials`) resolved per-batch and translated to glTF `alphaMode`/`doubleSided`, plus a static color tint/alpha-fade into `baseColorFactor` (see Per-vertex colors above), by `husk export` (`src/cmd_export.cpp`) — write-back to M2 not applicable (glTF-only tool) | ⬜ `M3SI` Instances → external `MaterialLibrary` (`.mtl3lib`) | ⬜ `MOMT`, `MOM3` (v3 override), `MOUV` (UV anim), per-face `MOPY`/`MPY2`/`MOBS` | ⬛ |
| Texture references (names/FileDataIDs) | 📖 `textures` array (`type`/`flags`/`filename`) + `textureCombos` lookup table resolved (`src/m2.cpp`); Legion+ `TXID` chunk FileDataIDs surfaced (`Header::textureFileDataIds`) — same non-resolved-to-a-path treatment as `SKID`, see the Sidecar row below | ⬜ indirect, via `MaterialLibrary` → compiled shader files (`GFAT`/`BLS`) — separate formats, not yet even scoped | ⬜ `MOTX` | ⬛ BLP is the referenced asset, not a referencer |
| Texture pixel data | ⬛ | ⬛ | ⬛ | 🚧 `blp/` (Python, `husk-blp` CLI) — header + mip table resolved, palette/DXT1/DXT5/BGRA decode to PNG done; DXT3 and JPEG content unimplemented (JPEG rare in BLP2 per the wiki; DXT3 unseen in this repo's real test data so far — not yet a confirmed-needed gap) |
| Animation sequences / tracks | 📖 `sequences` array resolved to real `M2Sequence` records (`id`/`variationIndex`/`duration`/`flags`, `src/m2.cpp`'s `parseSequences`); each inline bone's `translation`/`rotation`/`scale` `M2Track` resolved per-sequence (`resolveVec3TrackSequence`/`resolveQuatTrackSequence`) into real glTF `animation` clips by `husk export`, for sequences whose data lives in the M2 itself (`flags & 0x20`) — sequences whose data lives in an external `.anim` file, and `.skel`-sourced (non-inline) bones' tracks either way, aren't resolved (`AFID`/`BFID` FileDataIDs are surfaced by `husk info` but the files' content isn't parsed) | ❔ no sequence/track chunk documented in the fetched spec at all | ⬛ (`MOUV` texture-translation anim is the closest thing; counted under Materials) | ⬛ |
| Interaction points (attachments, cameras, events) | 🚧 `attachments`/`cameras`/`events` array counts/offsets read (`husk info`); records not dereferenced | ❔ not present in the fetched chunk list | ⬛ | ⬛ |
| Lights | 🚧 `lights` array count/offset read (`husk info`); records not dereferenced | ❔ | ⬜ `MOLT` + `MOLR`/`MOLS`/`MOLP` + Shadowlands lightset system (`MLSS`/`MLSP`/`MLSO`/`MLSK`), `MNLD` dynamic lights, legacy v14 `MOLM`/`MOLD` lightmaps | ⬛ |
| Particles / ribbons (effects) | 🚧 `particle_emitters`/`ribbon_emitters` array counts/offsets read (`husk info`); records not dereferenced; `EXPT`/`EXP2`/`TXAC` untouched | ❔ `M3PT` chunk family declared but wiki notes "not yet seen in files" | ⬜ `MPVD` particulate volumes, `MAVG`/`MAVD`/`MBVD` ambient/box volumes + their `*VR` reference lists | ⬛ |
| Fog / environment volumes | ⬛ | ⬛ | ⬜ `MFOG` + `MFED` extra data + `MFOB` fog objects | ⬛ |
| Liquid / water | ⬛ | ⬛ | ⬜ `MLIQ` | ⬛ |
| Portals / visibility culling | ⬛ | ⬛ | ⬜ `MOPV`/`MOPT`/`MOPR`/`MOPE`, `MOVV`/`MOVB` visible-block lists | ⬛ |
| Doodad / object placement (scene composition) | ⬛ | ⬛ | ⬜ `MODS`/`MODN`/`MODI`/`MODD`/`MODR` + `MDDI`/`MDDL` additional info | ⬛ |
| World/group structure (root+group files, skybox) | ⬛ | ⬛ single-file, no group split | ⬜ `MOGN`/`MOGI`/`MOGP`/`MOGX`/`GFID` + `MOSB`/`MOSI` skybox + `MGI2` group-info-v2 | ⬛ |
| Sidecar FileDataID resolution | 🚧 `SFID`/`AFID`/`BFID`/`PFID`/`SKID`/`TXID` → `.skin`/`.anim`/`.bone`/`.phys`/`.skel`/BLP textures — none of these FileDataIDs are resolved to a *WoW/CASC* path (no CASC/listfile access, by design, see the README's Usage section); `SKID`/`SFID`/`TXID`/`BFID`/`AFID` are all surfaced as raw IDs (`husk info`; `Header::skeletonFileId`/`skinFileDataIds`/`textureFileDataIds`/`boneFileDataIds`/`animFileIds`), and `SFID`/`TXID` additionally get a *local-directory* resolution convention -- `husk export`'s `--skin-dir <dir>`/`--textures <dir>` look for `<dir>/<FileDataID>.skin`/`.png`, a directory the user populates themselves (e.g. via `husk-blp`), never CASC. `.skin`/`.skel` paths can also still be given explicitly instead. `PFID` (`.phys`) isn't touched at all yet | ⬛ self-contained, no sidecars per spec | ⬜ `GFID` → group files | ⬛ |

**Not individually rowed above** (still real, just low-priority/niche —
tracked here so nothing's silently dropped): WMO's `MOQG`/`MOGX` per-face
`groundType` gameplay metadata; `MDAL`/`MOPB` material/prepass-batch
overrides; `MOMX` (structure entirely unknown, "just a guess" per the
wiki itself); `MPB*` (present in exactly one known alpha file, never read
by any shipped client — not planned). v14-alpha-only WMO chunks (`MOLV`,
`MOIN`, pre-1.0 `MOLM`/`MOLD`) are real but scoped out until there's an
actual reason to read a 2004-era WMO.

**Related sidecar/dependency formats** (their own files, not covered by
the matrix above, will need their own row set once work starts on them):
M2's `.skin` (mesh/LOD views), `.anim` (offloaded animation data),
`.bone`, `.phys`, `.skel`; M3's `.mtl3lib` (`MaterialLibrary`) and the
`GFAT`/`BLS` compiled-shader files it points to. All: not started.

## Roadmap: modern M2 → Blender, via glTF

The eventual goal is a real Blender import path — mesh, skeleton, textures,
materials, and animation, for a *modern* (Legion+ chunked) M2. husk doesn't
write a Blender addon itself; the target is a sensible glTF 2.0 export
(binary `.glb`, core PBR metallic-roughness material model) that Blender's
own built-in glTF importer can open unmodified. That keeps husk's job
scoped to "read WoW formats, write correct glTF" — Blender-side concerns
(addon UI, live reimport, etc.) are explicitly out of scope unless the
glTF path turns out to be insufficient.

The order below is a dependency chain, not a wishlist — each stage only
makes sense once the one before it works, and each is meant to be a
demoable milestone (something you can actually open in Blender and look
at), not an invisible internal refactor. Current status per piece is
tracked precisely in the [format support matrix](#format-support-matrix-m2--m3--wmo--blp)
above; this section is about *sequencing* that work, not duplicating it.

1. **Static mesh, no material. Done** — `husk export <file.m2> <file.skin>
   <out.glb>` (see [Usage](#usage) above). Resolves the `vertices` array's
   actual contents (`M2Vertex`: `pos`, `bone_weights[4]`, `bone_indices[4]`,
   `normal`, `tex_coords[2]`), not just the array's `(count, offset)` pair.
   M2 itself has no triangle indices; those come from a `.skin` file
   (LOD/submesh views) — its filename is given explicitly on the command
   line for now, not yet resolved automatically via the Legion+ `SFID`
   chunk (that's still open, see the format matrix's "Sidecar FileDataID
   resolution" row). Positions/normals/UVs/indices get written into a
   minimal glTF (no material, no image — Blender will render it flat gray).
   The Z-up (WoW) → Y-up (glTF) coordinate flip from wowdev.wiki's own note
   (`(X, Y, Z)` → `(X, -Z, Y)`) is applied in `src/gltf.cpp`'s `zUpToYUp`.
   Verified against `bloodelffemale.m2` + `bloodelffemale00.skin` (8061
   vertices, 10,458 triangles, glTF binary framing round-trips through
   tinygltf's own loader intact); **not yet verified**: actually opening the
   output in Blender (no Blender in the environment this was built in).
2. **Skeleton + skinning, still untextured. Done** — `husk export` now
   resolves the `bones` array (`M2CompBone`: `parent_bone`, `pivot`,
   `flags`, `key_bone_id`; the embedded `M2Track` animation blocks are
   skipped, not parsed — that's stage 6 below) and wires `M2Vertex`'s
   `bone_weights`/`bone_indices` into a glTF skin (`JOINTS_0`/`WEIGHTS_0`
   accessors, inverse bind matrices, a joint-node hierarchy from the bone
   parent chain, see `src/gltf.hpp`'s `Skeleton`). `M2Vertex.bone_indices`
   are direct indices into the M2's own `bones` array — confirmed against
   [pywowlib](https://github.com/wowdev/pywowlib)'s M2 writer, since the
   `.skin` file's own, *differently*-indirected `bones` lookup table (see
   its wiki page's debated remarks) is a separate, unrelated field this
   parser doesn't touch. Since M2's bind pose has no baked rotation/scale,
   each joint's inverse bind matrix is a plain translation by its negated
   absolute pivot — no matrix-chain composition needed. Success at this
   stage is "imports as an armature-bound mesh in the correct bind pose" —
   no animation playback yet. Verified against `bloodelffemale.m2` (119
   bones): exports cleanly, round-trips through tinygltf's own loader with
   a populated skin/joint hierarchy; **not yet verified** in Blender itself
   (same caveat as stage 1). Models with a genuinely empty inline `bones`
   array correctly fall back to an unskinned mesh, same as stage 1's
   output — but as stage 3 below explains, "inline `bones` array is empty"
   and "this model has no skeleton" turned out to be different things, so
   that fallback currently fires for some models that do have one.
3. **External skeleton sidecar (`.skel` via `SKID`). Done** — belongs here,
   not down in the animation stage, because what it resolves is structural
   (bind-pose bones), the same category of thing as stage 2 above — it
   just lives in a different file for some models. Legion+ (7.3+) can
   move a model's `bones` array out of the M2 entirely: an `SKID` chunk
   (`uint32_t SKeletonfileID`) points at a `.skel` file, itself chunked
   (`SKL1` header, `SKA1` attachments, `SKS1` sequences, optional `SKPD`
   for parent-skeleton dedup — see [wowdev.wiki
   M2/.skel](https://wowdev.wiki/M2/.skel)), whose `SKB1` chunk holds
   `M2Array<M2CompBone> bones` — the *identical* struct `m2::parseBones`
   already read for stage 2, just relocated (`src/skel.cpp` reuses it
   directly rather than re-parsing bones a second way). So this stage
   turned out to be exactly "find and read one more chunk + one more
   file," not a new struct to reverse-engineer. `husk export` takes the
   `.skel` path as an optional 4th argument, used only when the M2's own
   `bones` array is empty (a model with inline bones takes priority; a
   redundant `.skel` argument is noted on stderr and ignored, not an
   error); `husk info` now also flags the `SKID` file data ID when it's
   present and inline bones are empty, so the "0 bones" gap this stage
   closes doesn't have to be rediscovered by hand again. Deliberately
   **not** in scope for this stage: the `BFID` chunk (`.skel`'s or M2's)
   pointing at numbered `${basename}_${i}.bone` files — per wowdev.wiki,
   those replaced *per-bone animation track* data (the same category as
   `.anim`/`AFID`), not bind-pose structure, so they're a stage-6 concern,
   not this one. Verified against `bloodelffemale_hd.m2` +
   `bloodelffemale_hd.skel` in this repo's test data: resolves all 245
   bones from a 23.4 MB `.skel` file (dominated by embedded `M2Track`
   keyframe data this parser correctly skips over) in well under a
   second, and produces a glTF skin that round-trips through tinygltf's
   own loader the same as stage 2's inline-bones case.
4. **Textures: BLP → PNG. Done** — a hard prerequisite for materials to
   show anything other than gray, and genuinely separate work from M2
   parsing, so genuinely separate it's not even C++: `blp/` is a small
   Python package (`husk-blp <file.blp> <out.png> [--mip N]`, uv-managed,
   `nix develop` provides `uv` — see the flake and `blp/pyproject.toml`),
   not part of the `husk` binary. Split of responsibility inside it: the
   BLP2 container format itself (148-byte header + 1024-byte
   palette/JPEG-header region, mip offset/size tables) is hand-rolled and
   independently spec-transcribed, same rigor as husk's C++ modules — it's
   plain structured data, nothing fuzzy about it. Actual DXT1/DXT3/DXT5
   block decoding is *not* hand-rolled: `husk_blp/decode.py` wraps the raw
   compressed bytes from a mip level in a minimal synthetic DDS container
   (bit-for-bit the same block layout) and hands that to Pillow's own,
   battle-tested DDS reader, rather than reimplementing color/alpha
   interpolation math — confirmed pixel-correct against hand-built
   single-block fixtures first (`blp/tests/test_decode.py`), not assumed.
   Scoped to the three encodings this repo's real test data (1021 `.blp`
   files under `test_data/character/bloodelf/female/`) actually contains
   — Palettized (alpha depth 0), DXT1 (opaque), DXT5 (interpolated alpha)
   — checked empirically before writing any decode code, not assumed;
   uncompressed BGRA is also implemented (trivial, no library needed) even
   though this test data doesn't happen to use it. Explicitly deferred:
   DXT3 (unseen in this repo's test data, so not a confirmed-needed gap
   yet) and JPEG content (wiki: rare in BLP2). Palette alpha depth 4 is
   also deliberately unimplemented and raises a clear error rather than
   guessing — the wiki's own spec table doesn't clearly document that
   value's bit layout, unlike the other three depths. Verified against
   three real files, one per implemented DXT/palette encoding, output
   inspected directly (not just "didn't crash"): a DXT1 face texture, a
   DXT5 particle-effects atlas with working transparency, and a palettized
   face-detail texture — all visually correct, unmistakably the right
   image content, not garbage that merely happened to be the right
   dimensions.
5. **Materials. Done** — resolves the `.skin` file's `batches` array
   (`M2Batch`, wowdev.wiki M2/.skin#Texture_units, `src/skin.cpp`'s
   `Batch`) into one glTF material + primitive per batch: `batch ->
   submesh` (a slice of the already-resolved triangle-index buffer, via
   `Submesh.indexStart`/`indexCount`) `-> materialIndex` (M2's own
   `materials` array, `m2::Material`'s `flags`/`blendMode`) `->
   textureComboIndex` (through the header's `textureCombos` lookup table,
   wowdev.wiki's "Texture lookup table") `-> textures` array entry. WoW's
   `M2BLEND_*` blend mode collapses to glTF's three-way `alphaMode`
   (`0`→`OPAQUE`, `1`→`MASK`, everything else — real alpha blend plus the
   additive/multiply modes glTF's core material model has no equivalent
   for — `BLEND` as the closest approximation) and the material's
   "two-sided" render flag (`0x04`) becomes glTF `doubleSided`
   (`src/cmd_export.cpp`'s `alphaModeForBlend`). Deliberately **not**
   attempting real PBR authoring (roughness/metalness/normal maps) — WoW's
   own shader model doesn't map cleanly onto metallic-roughness, and
   faking plausible-looking values is a separate, later problem from
   "does the right blend mode show up on the right part of the mesh."
   One glTF material per *batch* rather than per M2 material index is
   deliberate: a batch is the thing that actually pins down both blend
   mode *and* texture together, and nothing in the format guarantees two
   batches sharing a `materialIndex` also share a texture (this repo's
   real test data happens to keep that 1:1, but the spec doesn't promise
   it) — some duplicate-looking glTF materials is a fair trade for never
   guessing.
   `baseColorTexture` embedding is real, not just metadata: husk still
   doesn't resolve a texture's FileDataID to a WoW/CASC path itself (same
   non-goal as `.skin`/`.skel`, see Usage below and the format matrix's
   Sidecar row) — but `husk export --textures <dir>` embeds an actual PNG
   when one already exists at `<dir>/<FileDataID>.png` (produced by
   `blp/`'s `husk-blp`, roadmap stage 4), closing the loop between the two
   tools without husk ever touching CASC or a listfile. Without
   `--textures`, materials still get the right `alphaMode`/`doubleSided`,
   just no image — both paths verified against real data (see Usage).
   Four follow-on pieces extend this stage rather than opening a new one:
   **vertex-color/transparency** (`M2Color`/`M2TextureWeight`, resolved
   into `baseColorFactor` as a *static* value only when the underlying
   `M2Track` is unambiguously constant — see the Design notes below for
   why that safeguard exists, and `src/m2.cpp`'s `Color`/`TextureWeight`);
   **the second UV set** (`TEXCOORD_1`, plus per-material `texCoord`
   selection via `textureCoordCombos`, pre-Cataclysm-only); **LOD
   auto-selection** (`SFID` + `husk export auto --skin-dir <dir>`,
   `LDV1`'s `lodCount` surfaced for information); and **`BFID`/`AFID`
   surfaced as raw FileDataIDs** (`husk info`), the same
   surface-now-resolve-later treatment `SKID` got before `.skel` support
   existed — real `.bone`/`.anim` *content* parsing is still stage 6
   below.
6. **Animation. Done, for inline bone tracks.** `husk export` resolves
   `sequences` into real `M2Sequence` records (`src/m2.cpp`'s
   `parseSequences`) and, for every *inline* bone (see stage 3's `.skel`
   caveat below), each of its `translation`/`rotation`/`scale` `M2Track`s
   into per-sequence keyframes (`resolveVec3TrackSequence`/
   `resolveQuatTrackSequence`) -- one glTF `animation` clip per sequence
   with real inline data (`M2Sequence.flags & 0x20`, wowdev.wiki: "the
   animation data is in the .m2 file"), covering every bone that actually
   has keyframes for it. Translation keyframes are the bind-pose local
   offset (`Skeleton::Joint::localTranslation`) plus the animated delta --
   glTF's animated translation *replaces* a node's translation at sampled
   times rather than adding to it, so the bind offset has to be baked into
   every keyframe. Rotation/scale keyframes are converted from WoW's Z-up
   space the same way positions are (see `src/cmd_export.cpp`'s
   `toGltf(m2::Quat)`/`toGltfScale`) -- derived from general
   change-of-basis principles and checked numerically against several test
   rotations rather than taken from an explicit wowdev.wiki formula (none
   is documented for this specific step). Verified against
   `bloodelffemale.m2`: 256 animation clips, 73,465 rotation keyframes, all
   finite and unit-norm (see the Usage section's verified-numbers
   paragraph) -- **not yet verified** in Blender itself (does a clip
   actually play back looking right, not just structurally valid data).
   **Deliberately out of scope for this stage**, left for a follow-on:
   external `.anim`-file sequences (`flags & 0x20` unset -- per wowdev.wiki
   these load from `"%s%04d-%02d.anim"`, resolved via the Legion+ `AFID`
   chunk, whose FileDataIDs plus each entry's `animId`/`subAnimId` are
   already surfaced as `Header::animFileIds`) and `.skel`-sourced (non-
   inline) bones' animation, which per wowdev.wiki's `.skel` article moves
   to a `.anim` file's `AFSB` chunk once a model has a `.skel` file at all
   -- `Bone::translationTrackOffset` et al. are still populated for
   `.skel` bones (parseBones can't tell the difference), but
   `buildAnimations` (`src/cmd_export.cpp`) only ever calls it for inline
   bones, since a `.skel` model's inline tracks are expected to be
   genuinely empty, not a source of real keyframes. `BFID` (on the M2
   itself, or inside a stage-3 `.skel` file, already surfaced as
   `Header::boneFileDataIds`) points at numbered `${basename}_${i}.bone`
   files which per wowdev.wiki replaced *per-bone* animation track data --
   likely an alternate delivery mechanism for the same kind of data this
   stage already resolves inline, not a separate concept; confirm that
   against a real `.bone` file (this repo's test data has 20 of them) when
   `.anim`/`.bone` content parsing actually starts.
7. **Output hardening.** Validate actual `.glb` output against the
   Khronos glTF-Validator, not just "Blender didn't crash on import."
   Decide the LOD/skin-profile policy (almost certainly: always emit the
   highest-detail skin profile, ignore the rest, at least until there's a
   concrete reason not to).

Explicitly not in this chain yet: WMO, M3, and anything in the
"write"/round-trip direction (a real Blender import *addon* rather than a
glTF file Blender happens to be able to open).

## Testing

Same two-tier split as `casc-tool`:

- **Pure-logic** (`tests/test_chunk.cpp`, `test_m2.cpp`, `test_skin.cpp`,
  `test_skel.cpp`) — synthetic buffers built field-by-field from the wiki
  spec, every offset cross-checked with a distinct sentinel value so a
  field landing at the wrong byte shows up as a specific failing `CHECK`,
  not a coincidental pass. `tests/test_gltf.cpp` takes a related but
  different approach: since `src/gltf.cpp` delegates the actual glTF
  binary framing to tinygltf rather than hand-rolling it, its tests
  round-trip `writeGlb()`'s output back through tinygltf's own loader and
  check the mesh data survived, rather than re-deriving byte offsets by
  hand. No real files needed for any of the above, always run.
- **Integration** (`tests/test_integration.cpp`) — runs the compiled
  `husk` binary against a real, game-extracted `.m2` (+ matching `.skin`,
  for `export`) as a subprocess. Deliberately asserts only on shape (exit
  code, "did it find some vertices", "is the output a plausibly-sized
  well-formed `.glb`"), not on any one model's specific field values —
  those belong in the synthetic tests. Also covers the failure paths: a
  `.skin` that doesn't belong to the given M2 must fail loudly, not
  silently misread; a model with an external `.skel` skeleton must produce
  a real skinned glTF skin, not a silent unskinned fallback. Skipped (not
  failed) unless the relevant env vars point at real files: `HUSK_TEST_M2`
  (bare `info` -- also checks `skin_file_data_ids`/`anim_file_ids` show up),
  plus `HUSK_TEST_SKIN` (`export`, also exercises the per-batch materials
  path -- checks for a plausible `alphaMode` spread and a `TEXCOORD_1`
  attribute, not exact values), `HUSK_TEST_MISMATCHED_SKIN` (the mismatch
  failure path), `HUSK_TEST_TEXTURES_DIR` (alongside
  `HUSK_TEST_M2`/`HUSK_TEST_SKIN` -- a directory of `husk-blp`-converted
  `<FileDataID>.png` files, to exercise `--textures`' real image
  embedding), `HUSK_TEST_SKIN_DIR` (alongside `HUSK_TEST_M2` -- a directory
  containing that model's own SFID-entry-0 `.skin`, renamed to its
  FileDataID, to exercise `auto` + `--skin-dir`'s LOD auto-selection), or
  `HUSK_TEST_SKEL_M2`/`HUSK_TEST_SKEL_SKIN`/`HUSK_TEST_SKEL` (the
  external-skeleton path, a separate model+trio from the others since it
  needs one with an `SKID` chunk). `test_data/` (gitignored) is a
  convenient local spot for these -- real, copyrighted game data extracted
  from your own install, never meant to be committed.

```
cmake --build build -j$(nproc)
./build/husk-tests                                    # pure-logic only
HUSK_TEST_M2=test_data/bloodelffemale.m2 \
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin \
  ./build/husk-tests                                  # + integration
```

`blp/` (Python) has its own, separate test suite -- same two-tier shape
(synthetic `test_header.py`/`test_decode.py`, always run; real-file
`test_integration.py`, skipped unless `HUSK_TEST_BLP_DXT1`/
`HUSK_TEST_BLP_DXT5`/`HUSK_TEST_BLP_PALETTE` point at real files):

```
cd blp
uv sync
uv run pytest                                          # pure-logic only
HUSK_TEST_BLP_DXT1=../test_data/character/bloodelf/female/bloodelffemale_hd_face_3500074.blp \
HUSK_TEST_BLP_DXT5=../test_data/character/bloodelf/female/bloodelffemale_hd_4530998.blp \
HUSK_TEST_BLP_PALETTE=../test_data/character/bloodelf/female/bloodelffemalefacelower10_00.blp \
  uv run pytest                                        # + integration
```

## Design notes

- **This format keeps growing new top-level chunks, so husk tracks the
  known set explicitly and flags anything outside it.** wowdev.wiki/M2#Chunks
  (fetched 2026-07-25) documents 30 top-level Legion+ chunk tags, spanning
  client build 7.0.1.20740 through an *unreleased* 12.0.0.63967 -- this
  format is still actively gaining chunks as of the wiki's most recent
  build coverage, not a settled target. `husk::readChunks` (`src/chunk.cpp`)
  is already tag-agnostic by construction -- an unrecognized tag is just
  skipped, never an error -- so a brand-new chunk in a future client build
  doesn't break parsing on its own. But "silently fine" isn't the same as
  "someone would notice a new chunk showed up that might matter" -- so
  `m2::Header::chunkTags` (every top-level tag found, populated in
  `src/m2.cpp`'s `resolveBlob`) plus `cmd_info.cpp`'s
  `documentedM2ChunkTags`/`isUndocumentedChunkTag` turn that into an actual
  diagnostic: `husk info` prints a `note:` line naming any chunk tag that
  isn't even in the known list, distinct from (and much rarer than) tags
  that are documented but husk just doesn't parse yet (`SFID`/`AFID`/`BFID`/
  `TXAC`/`LDV1`/etc., which show up in every real chunked file and print
  quietly under `chunks:` with no note). `tests/test_integration.cpp` turns
  this into a live canary against real game data (`HUSK_TEST_M2`): if a
  freshly re-extracted file ever trips the undocumented-tag note, that's
  the signal a client update shipped something new -- go update
  `documentedM2ChunkTags` and decide whether it needs real support, rather
  than the format quietly drifting out from under this codebase unnoticed.
  Adding real support for the *next* simple FileDataID-shaped chunk (the
  single most common shape new chunks take, see below) is meant to be
  cheap now too: `m2.cpp`'s `findFileDataIdChunk`/`findFileDataIdArrayChunk`
  generalize what `SKID`/`TXID` already do into one reusable pair of
  helpers instead of a hand-copied loop per chunk.
  **The recurring shapes new M2 chunks tend to take**, as read off the
  wiki's own chunk-by-chunk history (each section names the first client
  build it appeared in) -- useful for guessing what a *future* addition
  will look like before it exists, not just cataloguing the past:
  - *Filename → FileDataID indirection.* `SFID`/`AFID`/`BFID`/`PFID`/`SKID`/
    `TXID`/`RPID`/`GPID` all replaced a formerly computed/templated
    filename string (`${basename}_${i}.bone`, etc.) with a parallel
    FileDataID array, all introduced across Legion→BfA (7.0-8.1). This
    particular migration looks largely finished across the fields the
    wiki documents, but the underlying pattern -- "whatever's still
    filename-based somewhere else in the format is a candidate" -- isn't
    something that ever fully closes off.
  - *Small, additive, feature-specific chunks*, one or more per major
    content patch, each tied to one specific new rendering trick:
    `WFV1`/`WFV2`/`WFV3` (PBR-ish shading, first seen on a waterfall model
    in 8.2, later reused unrelated to waterfalls entirely), `EDGF` (edge
    fade), `NERF` (distance-based alpha falloff), `DETL`/`TEXL` (light
    shadow/cookie data), `PCOL` (11.1.7, player-housing collision --
    tied to an entire *upcoming* feature), `DPIV`. These are the "safe by
    default" case: purely additive, chunk-tag-gated, invisible to husk
    until husk actually needs what they carry.
  - *Chunks arrive in feature-correlated clusters, not evenly spaced.*
    `PABC`/`PADC`/`PSBC`/`PEDC` (all "parent [thing] data chunk") all
    landed together around 7.3, one per field of a single new
    parent-model/animation-blacklist subsystem -- a hint that the *next*
    format-affecting feature is more likely to show up as several related
    chunks in one patch than a single new tag in isolation.
  - *A schema-incompatible revision gets a brand-new tag, not a version
    bump on the old one.* `WFV1`→`WFV2`→`WFV3` are three separate tags for
    what's conceptually one evolving feature, unlike the flat header
    itself (which grows new `#if version >= X` fields in place, e.g.
    `name` going empty at 9.2.0.41462+, `textureCombinerCombos` being
    flag-gated). Practical implication: don't assume a future breaking
    change to something husk already reads announces itself as a version
    number to branch on -- it might show up as an entirely different chunk
    tag instead, which is exactly why `chunkTags`/`documentedM2ChunkTags`
    key off the tag, not the header version.
  **M2 vs M3, as a signal for the more extreme kind of future change:** M3
  (wowdev.wiki/M3, a separate, unrelated-in-spec-but-shared-tooling-lineage
  format) is chunked *all the way down* -- every logical section (mesh,
  each vertex attribute stream, batches, LOD, collision) is its own
  top-level chunk with a uniform 16-byte header (`chunkID`/`chunkSize`/two
  property fields used as element counts), not one flat blob of
  fixed-offset `M2Array`-pointer fields. M2's own Legion+ evolution has
  been "peel individual *sidecar* concerns off into top-level chunks one
  at a time" (exactly the FileDataID-indirection pattern above) while
  still keeping its *core* vertex/mesh/bone data inside one big `MD21`
  blob read the old fixed-offset way. If that convergence ever continues
  further, the plausible next step isn't another `#if version` field on
  the existing header -- it's the vertex/bone/texture arrays themselves
  eventually moving out into dedicated chunks the way M3's `VPOS`/`VNML`/
  `VINX`/etc. already do. That would be a structurally different kind of
  break than anything in this codebase today (a chunk-driven redesign of
  `m2::parseVertices`/`parseBones`, not new offset constants) -- not
  something to build for speculatively, but worth having named here so
  it's recognized immediately if the wiki ever documents it, rather than
  looking like an unrelated one-off.
- **Chunk tags are read literally, not reversed.** M2 is the odd one out
  among WoW's chunked formats (WMO/ADT reverse chunk tag bytes); a chunk
  written `MD21` in the file is matched against the literal string `"MD21"`
  here. Getting this backwards is a classic WMO/ADT-experience trap — see
  the comment in `src/chunk.hpp`.
- **Header fields are read via fixed byte offsets, not a packed C struct.**
  The header's tail is version/flag-conditional (`textureCombinerCombos`
  only exists if a flag bit is set), and relying on compiler struct layout
  for that is fragile. Every field read in `src/m2.cpp` is an explicit,
  bounds-checked `memcpy` at a named offset instead.
- **Both on-disk shapes (flat `MD20`, chunked `MD21`) funnel into the same
  header parser.** Once you have the `MD20` blob — whether that's the
  whole file or one chunk's payload — the byte layout is identical; the
  only difference is where the blob starts and what it's offsets are
  relative to. `m2::parseHeader` resolves that once, up front.
- **glTF output goes through tinygltf, not a hand-rolled writer.**
  `src/gltf.cpp` builds a `tinygltf::Model` and hands it to
  `TinyGLTF::WriteGltfSceneToStream` for the actual `.glb` binary framing
  (chunk headers, padding, JSON serialization) — one less from-scratch
  format implementation to keep correct, on a library that's already
  spec-compliant. tinygltf ships prebuilt (`nix/flake.nix`); `src/gltf.cpp`
  only includes `tiny_gltf.h` and links against it, it does **not** define
  `TINYGLTF_IMPLEMENTATION` itself (that would double-define every symbol
  already compiled into `libtinygltf.a`).
- **`.skin` files are read as a co-equal sidecar of M2, not bolted on.**
  `src/skin.hpp`/`skin.cpp` follow the same fixed-offset,
  bounds-checked-`memcpy`, independently-spec-transcribed-tests approach as
  `src/m2.cpp` — see `tests/test_skin.cpp`'s header comment for the
  `M2SkinProfile`/`M2SkinSection`/`M2Batch` offsets. `vertices`/`indices`
  (the triangle-index lookup) and `submeshes`/`batches` (the
  material/texture linkage, roadmap stage 5) are read -- what `husk
  export` needs; `.skin`'s own `bones` field is a *different*,
  per-submesh-indirected bone lookup than `M2Vertex.bone_indices` (see the
  next bullet) and stays unread, since stage 2 already established the
  direct-index reading is correct and this indirected table isn't needed
  for anything husk currently does.
- **`M2CompBone`'s three embedded animation tracks are skipped, not
  parsed.** Per wowdev.wiki M2#Bones, each bone struct carries a
  `translation`/`rotation`/`scale` `M2Track<T>` (60 bytes total) before its
  `pivot` field. `m2::parseBones` (`src/m2.cpp`) knows their combined byte
  size precisely -- `M2Track<T>` is always 20 bytes regardless of `T`, see
  `tests/test_m2.cpp`'s header comment for why -- to skip straight to
  `pivot` without resolving what's actually a stage-6 (animation) concern.
  Getting `M2Vertex.bone_indices`' semantics right mattered enough to
  verify independently: they're direct indices into M2's own `bones` array,
  confirmed by reading [pywowlib](https://github.com/wowdev/pywowlib)'s M2
  writer (`m2_file.py`) rather than trusting a search-engine summary that
  claimed the opposite (likely conflating M2 with the unrelated Warcraft 3
  MDX format) -- see the roadmap stage 2 entry above for the specifics.
- **`.skel` reuses `m2::parseBones` outright instead of re-implementing
  bone parsing a second time.** `src/skel.hpp`/`skel.cpp` are deliberately
  thin: find the `SKB1` chunk (`husk::readChunks`, the same generic
  container M2 itself uses), read its 16-byte header for the `bones`
  array, then hand that array and the chunk's own payload bytes straight
  to `m2::parseBones` -- `SKB1.bones` is `M2Array<M2CompBone>`, the
  *identical* struct, just with offsets relative to the chunk's payload
  instead of the MD20 blob. No parallel bone-struct implementation to
  keep in sync with the real one. `husk export` only reaches for a given
  `.skel` path when the M2's own inline `bones` array is empty; if it
  isn't, the `.skel` argument is noted on stderr and ignored rather than
  erroring, on the assumption a model wouldn't legitimately have both.
- **`M2Track<T>` static-value resolution only fires when a track is
  unambiguously constant -- exactly one animation sub-array, exactly one
  keyframe in it (`src/m2.cpp`'s `constantTrackValueOffset`, used by
  `Color`/`TextureWeight` for the vertex-color/transparency feature).
  This was tightened after a real bug, not written this way from the
  start: an earlier version read element `[0][0]` unconditionally
  (matching the wiki's own worked example for how to walk an `M2Track`),
  which for a real `bloodelffemale.m2` batch meant reading *sequence 0's*
  alpha keyframe -- `0`, fully transparent -- as if it were a sensible
  default. That specific track turned out to have 339 sub-arrays (one per
  `M2Sequence`), i.e. genuinely per-sequence animated data with no single
  "default," and would have silently exported an invisible model.
  Requiring both counts to be exactly 1 is what the wiki's own "blocks
  that use global sequences also only have one track" actually describes
  as non-animated; anything looser risks the same mistake for a different
  model. See `tests/test_m2.cpp`'s regression test (named after this
  exact bug) and the Usage section's verified-numbers paragraph for how
  the fix was checked against real data.
- **LOD auto-selection (`--skin-dir`) and texture embedding (`--textures`)
  share one convention on purpose: a local directory keyed by
  FileDataID, never CASC.** `husk export`'s `auto` .skin path reads the
  M2's own `SFID` chunk and looks for `<dir>/<FileDataID>.skin`; this is
  the exact same shape as `--textures <dir>` already used for
  `<dir>/<FileDataID>.png`. Neither one resolves a FileDataID to a real
  WoW/CASC path -- the directory is something the user populates
  themselves (a `.skin` extracted via `casc-tool`, a PNG produced by
  `husk-blp`), so the non-goal stated elsewhere in this README ("husk
  never touches CASC storage itself") holds for both without exception.
- **`M2Sequence`'s record size is 64 bytes, not the 36 a literal reading of
  the wiki's own struct listing suggests -- caught by decoding real data,
  not by re-reading the spec harder.** wowdev.wiki's `M2Sequence` listing
  includes a line `M2Bounds bounds;` with no offset comment, sitting
  directly before an annotation `/*0x20*/ int16_t variationNext` that's
  consistent with `bounds` *not* being there at all (id/variationIndex/
  duration/movespeed/flags/frequency/padding/replay/blendTimeIn/
  blendTimeOut already sum to exactly 0x20). That's genuinely ambiguous
  from the text alone -- it could be a real 28-byte field the wiki forgot
  to re-number after inserting, or a stale leftover annotation. Deciding
  it either way from the prose would have been a guess; decoding all 339
  of `bloodelffemale.m2`'s real sequences at both candidate strides wasn't:
  36 bytes produces plausible values for roughly every other record and
  outright garbage for the rest (`variationIndex` in the tens of
  thousands, `duration` in the billions of milliseconds), while 64 bytes
  decodes every single one cleanly (small ids, millisecond durations).
  `src/m2.hpp`'s `Sequence` doc comment carries this derivation; get this
  wrong and every other `Sequence` field this parser reads (id, duration,
  flags) silently comes from the wrong byte range for roughly half of a
  real model's sequences -- not a crash, just quietly wrong data, the same
  failure shape as the `M2Track[0][0]` bug below.
- **Bone rotation/scale keyframes need their own Z-up→Y-up conversion,
  distinct from (and not documented anywhere as explicitly as) positions'
  `zUpToYUp`.** No wowdev.wiki page spells out the M2-bone-track-specific
  formula. What's used instead (`src/cmd_export.cpp`'s `toGltf(m2::Quat)`/
  `toGltfScale`) is derived from the general rule for re-expressing a
  rotation/scale under a change of basis that's itself a proper rotation
  (`zUpToYUp`'s `(X, -Z, Y)` permutation has determinant +1): a rotation
  quaternion gets the *same* permutation applied to its vector part only
  (scalar/`w` untouched); a scale vector gets the same permutation with
  signs dropped (Y and Z swapped, unsigned -- conjugating a diagonal scale
  matrix by a signed-permutation change of basis just permutes the
  diagonal, the signs cancel). Checked numerically, not just asserted:
  computed both the "correct by definition" conjugated 3×3 rotation
  matrix (`R · Q · Rᵀ`) and the permutation-shortcut quaternion for several
  test rotations (axis-aligned and an arbitrary compound one) and confirmed
  they produce bit-identical matrices, and separately confirmed a
  conjugated diagonal scale matrix's entries match the unsigned-swap
  shortcut. This is weaker evidence than a cited spec formula would be --
  it's correct *given* that WoW bones use plain parent-relative TRS
  composition with no separate DCC-style "pivot" concept beyond what
  `M2CompBone.pivot` already contributes as the bind-pose joint position
  (the same assumption `buildSkeleton`, stage 2, already made and that
  this stage's real-data check -- unit-norm quaternions, sane durations --
  is consistent with, not proof of). See the Usage section's
  verified-numbers paragraph for what was actually checked end to end.
- **Not built yet:** dereferencing `attachments`/`events`/`lights`/
  `cameras`/`ribbon_emitters`/`particle_emitters`/the collision arrays into
  their actual `M2Attachment`/`M2Event`/`M2Light`/`M2Camera`/`M2Ribbon`/
  `M2Particle` records (only the header's `(count, offset)` `M2Array`
  descriptors themselves are read, same as most of these before roadmap
  stage 6 -- see `husk info`'s output), `PFID` (`.phys`, the one Legion+
  sidecar chunk with no FileDataID surfaced at all yet --
  `SKID`/`SFID`/`TXID`/`BFID`/`AFID` all are, see the format matrix's
  Sidecar row), actual `.bone`/`.anim` *content* parsing (their
  FileDataIDs are surfaced, the files themselves aren't fetched or read --
  this is what gates external-`.anim`-sequence and `.skel`-sourced-bone
  animation, see roadmap stage 6), `.skin`'s own `bones` field (a
  different, per-submesh-indirected bone lookup, see the design note
  above), `.skel`'s own `key_bone_lookup` field and its
  `SKL1`/`SKA1`/`SKS1`/`SKPD` chunks, M3, WMO, and any form of
  writing/conversion back into WoW's native formats (only glTF export is
  in scope, see the roadmap above).
- **`.reference/`** (gitignored) holds a clone of
  [M2Mod/m2mod](https://github.com/M2Mod/m2mod) for cross-checking
  anything ambiguous in the wiki — not a build dependency, not vendored,
  just something to grep when the spec is unclear.
- **`blp/` is Python, deliberately, and deliberately not part of the
  `husk` C++ binary.** Two independent reasons, not one: BLP decoding
  needs real image-library maturity (correct PNG writing, a trustworthy
  DXT/BC block decoder) that C++ doesn't have a clean equivalent of
  already in this project the way tinygltf covers glTF output, and
  texture conversion stays a separate process boundary even now that
  `husk export --textures` actually embeds the PNGs it produces (roadmap
  stage 5) -- husk reads a PNG file `husk-blp` already wrote and named by
  FileDataID, it doesn't invoke `husk-blp` as a subprocess or link against
  Pillow, so the two tools/languages never actually touch. `husk_blp/decode.py`'s DXT1/DXT3/DXT5 handling in particular leans on
  a library rather than hand-rolled block math: it wraps the raw
  compressed bytes in a minimal synthetic DDS container (the same S3TC
  block layout, just a different file wrapper) and hands that to
  Pillow's own DDS reader. Confirmed pixel-correct against a hand-built
  single-block fixture before relying on it for anything real (see
  `blp/tests/test_decode.py`'s module comment) -- not assumed correct
  just because Pillow is a trusted library in general.

## Disclaimer

This tool is co-coded by AI, verified by a massively autistic developer —
every field-offset claim here was checked against the real spec and a real
game file, not taken on faith.
