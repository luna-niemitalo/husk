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

## Usage

```
husk info <file.m2>
husk export <file.m2> <file.skin> <output.glb>
```

`info` parses the header and prints: whether the file is pre-Legion (flat
`MD20`) or Legion+ (chunked, `MD21`-wrapped), the version and its
best-guess expansion label, the model's internal name, and record counts
(sequences, bones, vertices, textures, materials) read out of the header's
`M2Array` fields.

`export` resolves the M2's `vertices` array to actual `M2Vertex` records
(position, normal, both UV sets, plus the raw bone weights/indices) and the
given `.skin` file's two-level triangle-index lookup (see `src/skin.hpp`),
converts WoW's Z-up coordinates to glTF's Y-up, and writes a minimal
single-primitive glTF binary -- positions/normals/UVs/indices, no material,
no image, no skin/skeleton yet. That's [roadmap stage
1](#roadmap-modern-m2--blender-via-gltf) below. husk doesn't resolve the
`.skin` filename itself (no `SFID`-FileDataID lookup yet) -- pass the path
to whichever `.skin` matches the M2's LOD you want (e.g. `bloodelffemale.m2`
pairs with `bloodelffemale00.skin`, its LOD0 -- not an `_hd`-suffixed file,
which belongs to a separate, much higher-poly HD-variant M2).

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
8061 vertices and 10,458 triangles from that same pair and writes a ~375 KiB
`.glb`. Also stress-tested against the same character's HD variant,
`bloodelffemale_hd.m2` (195,498 vertices!) + its matching
`bloodelffemale_hd00.skin`, producing a ~6.8 MB `.glb` with 45,418
triangles -- exercises the same code path at ~24x the vertex count.
That HD pairing is also a real example of the mismatch check earning its
keep: `bloodelffemale_hd00.skin`'s `vertices` lookup table references M2
global vertex indices up to 32938, which only makes sense against
`bloodelffemale_hd.m2`'s 195,498 vertices, not the base `bloodelffemale.m2`
(8061 vertices) -- pairing it with the wrong model is exactly the failure
mode `cmd_export.cpp`'s bounds check exists to catch loudly instead of
silently misreading. Verified so far: the glTF binary framing round-trips
through tinygltf's own loader intact (`tests/test_gltf.cpp`) and file sizes
add up correctly for both real models; **not yet verified**: actually
opening either output in Blender (no Blender available in the environment
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
| Chunk container / magic detection | 🚧 `MD20`/`MD21` detected, generic chunk reader in `src/chunk.*` | ⬜ (chunked like WMO, 16-byte chunk header + property fields) | ⬜ (`MOMO` wrapper, Legion+) | ⬛ flat header, not chunked |
| Header / global metadata | 🚧 `husk info` reads magic/version/name/flags/bounding boxes/array counts | ⬜ `M3DT` (376 bytes: flags, 2 bounding boxes, particle count) | ⬜ `MOHD` (counts, ambient color, WMOID, bounding box, flags) | ⬜ 148-byte header + 1024-byte palette/JPEG region |
| Skeleton / bone hierarchy | ⬜ `bones` array (offsets read, contents not resolved) | ❔ no joint-hierarchy chunk documented — only per-vertex weights/bind-poses exist (`VWTS`/`VIBP`) | ⬛ no skeleton | ⬛ |
| Vertex skinning (bone weights/indices) | 📖 read as part of `M2Vertex` (`bone_weights[4]`/`bone_indices[4]`), not yet wired into a glTF skin | ⬜ `VWTS` (weights), `VIBP` (inverse bind poses) | ⬛ | ⬛ |
| Mesh geometry (positions, indices) | 📖 `vertices` array resolved to real `M2Vertex` records (`src/m2.cpp`); triangle indices resolved via one explicitly-given `.skin` file (`src/skin.cpp`); exported to glTF via `husk export` | ⬜ `VPOS`/`VINX`/`VGEO`+`Geoset`/`LODS`/`RBAT` | ⬜ `MOVT`/`MOVI`/`MOVX`, `MOBA` batches, `MORI`/`MORB` triangle-strip variants | ⬛ |
| Normals | 📖 part of `M2Vertex`, resolved | ⬜ `VNML` | ⬜ `MONR` | ⬛ |
| UV / texture coordinates | 📖 both `tex_coords[2]` sets resolved (only set 0 currently exported to glTF) | ⬜ `VUV0`–`VUV5` (up to 6 sets) | ⬜ `MOTV` | ⬛ |
| Tangents | ❔ not in the documented base header — appears to be runtime-computed, not stored | ⬜ `VTAN` | ⬜ `MOTA` (often auto-generated client-side for shaders 10/14) | ⬛ |
| Per-vertex colors | ⬛ M2's `Colors` header field is animated material tint, not per-vertex mesh color | ⬜ `VCL0`/`VCL1` | ⬜ `MOCV`/`MOC2` | ⬛ |
| LOD / mesh views | 🚧 one `.skin` file's `vertices`/`indices` lookup tables read directly (`src/skin.cpp`); `.skin` filename must be given explicitly, no `SFID` resolution yet | ⬛ `LODS` folds LOD into the one file, no sidecar | ⬛ (`GFID`'s `Flag_Lod` is a different, coarser concept — tracked under World/group structure) | ⬜ mip pyramid — tracked under Texture pixel data below, not here |
| Collision / physics | 🚧 `bounding_box`/`collision_box` fields read; `.phys` sidecar (`PFID`) untouched | ⬜ `M3CL` collision mesh (`CPOS`/`CNML`/`CINX`) | ⬜ `MOBN`/`MOBR` BSP tree, `MCVP` convex volumes, `MOPL` terrain-cutting planes | ⬛ |
| Materials | ⬜ `materials` array | ⬜ `M3SI` Instances → external `MaterialLibrary` (`.mtl3lib`) | ⬜ `MOMT`, `MOM3` (v3 override), `MOUV` (UV anim), per-face `MOPY`/`MPY2`/`MOBS` | ⬛ |
| Texture references (names/FileDataIDs) | ⬜ `textures` array + combo lookup tables | ⬜ indirect, via `MaterialLibrary` → compiled shader files (`GFAT`/`BLS`) — separate formats, not yet even scoped | ⬜ `MOTX` | ⬛ BLP is the referenced asset, not a referencer |
| Texture pixel data | ⬛ | ⬛ | ⬛ | ⬜ header, mip table, palette/DXT1/DXT3/DXT5/BGRA/JPEG decode |
| Animation sequences / tracks | 🚧 `sequences` array count read, no track/keyframe contents | ❔ no sequence/track chunk documented in the fetched spec at all | ⬛ (`MOUV` texture-translation anim is the closest thing; counted under Materials) | ⬛ |
| Interaction points (attachments, cameras, events) | ⬜ `attachments`/`cameras`/`events` arrays | ❔ not present in the fetched chunk list | ⬛ | ⬛ |
| Lights | ⬜ `lights` array | ❔ | ⬜ `MOLT` + `MOLR`/`MOLS`/`MOLP` + Shadowlands lightset system (`MLSS`/`MLSP`/`MLSO`/`MLSK`), `MNLD` dynamic lights, legacy v14 `MOLM`/`MOLD` lightmaps | ⬛ |
| Particles / ribbons (effects) | ⬜ `particle_emitters`/`ribbon_emitters` arrays + `EXPT`/`EXP2`/`TXAC` | ❔ `M3PT` chunk family declared but wiki notes "not yet seen in files" | ⬜ `MPVD` particulate volumes, `MAVG`/`MAVD`/`MBVD` ambient/box volumes + their `*VR` reference lists | ⬛ |
| Fog / environment volumes | ⬛ | ⬛ | ⬜ `MFOG` + `MFED` extra data + `MFOB` fog objects | ⬛ |
| Liquid / water | ⬛ | ⬛ | ⬜ `MLIQ` | ⬛ |
| Portals / visibility culling | ⬛ | ⬛ | ⬜ `MOPV`/`MOPT`/`MOPR`/`MOPE`, `MOVV`/`MOVB` visible-block lists | ⬛ |
| Doodad / object placement (scene composition) | ⬛ | ⬛ | ⬜ `MODS`/`MODN`/`MODI`/`MODD`/`MODR` + `MDDI`/`MDDL` additional info | ⬛ |
| World/group structure (root+group files, skybox) | ⬛ | ⬛ single-file, no group split | ⬜ `MOGN`/`MOGI`/`MOGP`/`MOGX`/`GFID` + `MOSB`/`MOSI` skybox + `MGI2` group-info-v2 | ⬛ |
| Sidecar FileDataID resolution | ⬜ `SFID`/`AFID`/`BFID`/`PFID` → `.skin`/`.anim`/`.bone`/`.phys` | ⬛ self-contained, no sidecars per spec | ⬜ `GFID` → group files | ⬛ |

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
2. **Skeleton + skinning, still untextured.** Resolve the `bones` array
   (`M2CompBone`: parent index, pivot, flags) and wire `M2Vertex`'s
   `bone_weights`/`bone_indices` into a glTF skin (`JOINTS_0`/`WEIGHTS_0`
   accessors, inverse bind matrices, a joint-node hierarchy from the bone
   parent chain). Success at this stage is "imports as an armature-bound
   mesh in the correct bind pose" — no animation playback yet.
3. **Textures: BLP → PNG.** A hard prerequisite for materials to show
   anything other than gray, and genuinely separate work from M2 parsing
   — BLP is its own header/mip-table/pixel format (palette, DXT1/DXT3/
   DXT5, uncompressed BGRA, or JPEG; see the format matrix). Scope the
   first pass to whatever encoding the actual test models use (check
   before assuming DXT everywhere) and defer JPEG, which the wiki notes
   is rare in BLP2.
4. **Materials.** Resolve the `materials` array (blend mode, render
   flags) and the texture-unit → texture/material linkage that lives in
   the `.skin` file, then translate WoW's blend modes into glTF's
   `alphaMode` (`OPAQUE`/`MASK`/`BLEND`) with the resolved texture as
   `baseColorTexture`. Deliberately **not** attempting real PBR authoring
   (roughness/metalness/normal maps) in this first pass — WoW's own
   shader model doesn't map cleanly onto metallic-roughness, and faking
   plausible-looking values is a separate, later problem from "does the
   right texture show up with the right blend mode."
5. **Animation.** Resolve each bone's `M2Track` (translation/rotation/
   scale keyframes) and map one sequence at a time into glTF `animation`
   channels targeting joint nodes — get a single idle/stand loop playing
   correctly before attempting the full `sequences` list. Has a real
   extra wrinkle: per wowdev.wiki, a sequence loads from an external
   `.anim` file whenever `(M2Sequence.flags & 0x130) == 0`, resolved via
   the Legion+ `AFID` chunk (`ChunkedAnimFiles` flag) rather than living
   inline in the M2 — so this stage also needs `.anim` sidecar support,
   not just inline track parsing.
6. **Output hardening.** Validate actual `.glb` output against the
   Khronos glTF-Validator, not just "Blender didn't crash on import."
   Decide the LOD/skin-profile policy (almost certainly: always emit the
   highest-detail skin profile, ignore the rest, at least until there's a
   concrete reason not to).

Explicitly not in this chain yet: WMO, M3, and anything in the
"write"/round-trip direction (a real Blender import *addon* rather than a
glTF file Blender happens to be able to open).

## Testing

Same two-tier split as `casc-tool`:

- **Pure-logic** (`tests/test_chunk.cpp`, `test_m2.cpp`, `test_skin.cpp`) —
  synthetic buffers built field-by-field from the wiki spec, every offset
  cross-checked with a distinct sentinel value so a field landing at the
  wrong byte shows up as a specific failing `CHECK`, not a coincidental
  pass. `tests/test_gltf.cpp` takes a related but different approach: since
  `src/gltf.cpp` delegates the actual glTF binary framing to tinygltf
  rather than hand-rolling it, its tests round-trip `writeGlb()`'s output
  back through tinygltf's own loader and check the mesh data survived,
  rather than re-deriving byte offsets by hand. No real files needed for
  any of the above, always run.
- **Integration** (`tests/test_integration.cpp`) — runs the compiled
  `husk` binary against a real, game-extracted `.m2` (+ matching `.skin`,
  for `export`) as a subprocess. Deliberately asserts only on shape (exit
  code, "did it find some vertices", "is the output a plausibly-sized
  well-formed `.glb`"), not on any one model's specific field values —
  those belong in the synthetic tests. Also covers the failure path: a
  `.skin` that doesn't belong to the given M2 must fail loudly, not
  silently misread. Skipped (not failed) unless `HUSK_TEST_M2` (and, for
  the `export` cases, `HUSK_TEST_SKIN`) points at real files.

```
cmake --build build -j$(nproc)
./build/husk-tests                                    # pure-logic only
HUSK_TEST_M2=/tmp/bloodelffemale.m2 \
HUSK_TEST_SKIN=/tmp/bloodelffemale00.skin \
  ./build/husk-tests                                  # + integration
```

## Design notes

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
  `M2SkinProfile` offsets. Only the `vertices`/`indices` lookup tables are
  read (what `husk export` needs); `bones`/`submeshes`/`batches` wait for
  the skinning and materials stages.
- **Not built yet:** parsing most of the fixed header past what's
  currently read (attachments, events, lights, cameras, particles, ...),
  resolving most array offsets to their pointed-to records (`bones`,
  `textures`, `materials`, `sequences`, ... — `vertices` is the one
  exception, resolved for the mesh-export stage), the Legion+ sidecar
  chunks (`SFID`/`AFID`/`BFID`/`PFID`/etc. — `husk export` takes a `.skin`
  path explicitly instead of resolving `SFID` itself), `.skin`'s own
  `bones`/`submeshes`/`batches` fields, M3, WMO, and any form of
  writing/conversion back into WoW's native formats (only glTF export is
  in scope, see the roadmap above).
- **`.reference/`** (gitignored) holds a clone of
  [M2Mod/m2mod](https://github.com/M2Mod/m2mod) for cross-checking
  anything ambiguous in the wiki — not a build dependency, not vendored,
  just something to grep when the spec is unclear.

## Disclaimer

This tool is co-coded by AI, verified by a massively autistic developer —
every field-offset claim here was checked against the real spec and a real
game file, not taken on faith.
