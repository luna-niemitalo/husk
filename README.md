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
```

Parses the header and prints: whether the file is pre-Legion (flat `MD20`)
or Legion+ (chunked, `MD21`-wrapped), the version and its best-guess
expansion label, the model's internal name, and record counts (sequences,
bones, vertices, textures, materials) read out of the header's `M2Array`
fields.

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
| Vertex skinning (bone weights/indices) | ⬜ part of `M2Vertex` | ⬜ `VWTS` (weights), `VIBP` (inverse bind poses) | ⬛ | ⬛ |
| Mesh geometry (positions, indices) | ⬜ `vertices` array | ⬜ `VPOS`/`VINX`/`VGEO`+`Geoset`/`LODS`/`RBAT` | ⬜ `MOVT`/`MOVI`/`MOVX`, `MOBA` batches, `MORI`/`MORB` triangle-strip variants | ⬛ |
| Normals | ⬜ | ⬜ `VNML` | ⬜ `MONR` | ⬛ |
| UV / texture coordinates | ⬜ | ⬜ `VUV0`–`VUV5` (up to 6 sets) | ⬜ `MOTV` | ⬛ |
| Tangents | ❔ not in the documented base header — appears to be runtime-computed, not stored | ⬜ `VTAN` | ⬜ `MOTA` (often auto-generated client-side for shaders 10/14) | ⬛ |
| Per-vertex colors | ⬛ M2's `Colors` header field is animated material tint, not per-vertex mesh color | ⬜ `VCL0`/`VCL1` | ⬜ `MOCV`/`MOC2` | ⬛ |
| LOD / mesh views | ⬜ needs `.skin` sidecar files, resolved via `SFID` | ⬛ `LODS` folds LOD into the one file, no sidecar | ⬛ (`GFID`'s `Flag_Lod` is a different, coarser concept — tracked under World/group structure) | ⬜ mip pyramid — tracked under Texture pixel data below, not here |
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

## Testing

Same two-tier split as `casc-tool`:

- **Pure-logic** (`tests/test_chunk.cpp`, `test_m2.cpp`) — synthetic
  buffers built field-by-field from the wiki spec, every offset in the
  header cross-checked with a distinct sentinel value so a field landing
  at the wrong byte shows up as a specific failing `CHECK`, not a
  coincidental pass. No real files needed, always run.
- **Integration** (`tests/test_integration.cpp`) — runs the compiled
  `husk` binary against a real, game-extracted `.m2` as a subprocess.
  Deliberately asserts only on shape (exit code, "did it find some
  vertices"), not on any one model's specific field values — those belong
  in the synthetic tests. Skipped (not failed) unless `HUSK_TEST_M2` points
  at a real file.

```
cmake --build build -j$(nproc)
./build/husk-tests                                    # pure-logic only
HUSK_TEST_M2=/tmp/bloodelffemale.m2 ./build/husk-tests  # + integration
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
- **Not built yet:** parsing anything past the fixed header this MVP reads
  (attachments, events, lights, cameras, particles, ...), the Legion+
  sidecar chunks (`SFID`/`AFID`/`BFID`/`PFID`/etc. — see the wiki page for
  what they carry), actually resolving array offsets to their pointed-to
  records, M3, WMO, and any form of writing/conversion. This is a header
  reader, nothing more, yet.
- **`.reference/`** (gitignored) holds a clone of
  [M2Mod/m2mod](https://github.com/M2Mod/m2mod) for cross-checking
  anything ambiguous in the wiki — not a build dependency, not vendored,
  just something to grep when the spec is unclear.

## Disclaimer

This tool is co-coded by AI, verified by a massively autistic developer —
every field-offset claim here was checked against the real spec and a real
game file, not taken on faith.
