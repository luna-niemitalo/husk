# wowdev.wiki findings — BLS / GFAT (client shader containers)

Current, correct facts only. Every fact is checked against all 877 `.bls`
files in the 12.1.0 corpus (`/media/luna/data/wow_export/shaders/`).
`tools/export_shaders.py` relies on each container-layout fact and fails a
file loudly, with expected and actual values, if one stops holding. The
block-header, hash and `RDEF` facts come from its output manifests
(`../WIKI_FINDINGS_HISTORY.md` §19, §20). What the shaders themselves compute
is in `M2/rendering.md`, `ADT.md`, `WORLD.md` and `RENDERING.md`.

## Inventory (12.1.0)

| Container | Version | Files |
|---|---|---|
| GXSH (`HSXG` on disk) | `0x1000E` | 862 |
| GXSH | `0x1000C` (stale: `compute/dx_5_0/guidedfilter{,h,v}`, `pixel/dx_5_0/trianglearea61`, `raytracing/dx_5_0/shadowrt`) | 5 |
| GFAT (`TAFG` on disk), each wrapping a DX50 and a DX60 GXSH `0x1000E` | `0x1000B` | 10 |

The `mtl_1_1` directories are empty in this extraction.

## GXSH `0x1000E` ("BLS v1.14+")

The wiki's struct is right as far as it goes. What it leaves open:

- The 40-byte header is followed by `nShaders` 24-byte slots (`offset`,
  `size`, 16-byte hash), then `lastOffsetPlusSize`, then the chunk offset
  table. `ofsCompressionChunks == 40 + 24*nShaders + 4` and
  `ofsCompressedData == ofsCompressionChunks + 4*(nCompressedChunks+1)` on
  every file.
- The chunk offset table's last entry is the end of the last chunk, and
  `ofsCompressedData + last == file size`.
- The inflated stream's length equals `lastOffsetPlusSize`.
- A slot's `offset`/`size` index into the **inflated** stream.
- An uncompiled slot is `offset 0xFFFFFFFF, size 0`.
- Compiled slots share blobs heavily. `pixel/dx_5_0/illum`: 2048 slots,
  1024 compiled, 386 distinct blobs. Slots that share an offset always
  share the size.
- Each blob is a block header, then a DXBC container, then 0 or 4
  trailing bytes. The block header is 96 bytes on every `0x1000E` blob;
  its length isn't stored, so the exporter finds the DXBC magic.
- `0x1000E` DX50 blobs carry `SHEX` (DXBC-TPF); DX60 blobs carry `DXIL`.
- The DXBC is standard once the stream is inflated, but reflection is
  stripped: no blob in either API has an `RDEF` part. DX50 parts are
  `ISGN`/`OSGN`/`SHEX` (9,701 of 9,742), sometimes plus `SFI0` or as
  `ISG1`/`OSG1`. So DX50 constant buffers and textures have register
  numbers only, never names.
- DX60 DXIL keeps more. Variable names are stripped (`!""`), but every
  constant and structured buffer keeps its HLSL type name and full
  struct layout (`cb_scene_data` → `SceneData { PSFog[12], … }`,
  `StructuredBuffer<ShaderLight>`, `M2Vertex`, `WMOVertex`, …), and so do
  groupshared variables (`gs_FFX_PARALLELSORT_Histogram`). The ray-tracing
  library `shadowrt` also keeps resource and entry-point names
  (`SceneBVH`, `LinearDepth`, `RayGen`, `AlphaTestShadowCasterHit`).
  `dxc -dumpbin` prints all of it; the full table is
  `../SHADER_FINDINGS/notes/dxil_names.md`.
- The slot hash names the compiled **program**, not the permutation:
  every slot sharing a blob has the same hash (9,742/9,742 DX50 blobs),
  and no two blobs in a file share one. It differs between the DX50 and
  DX60 compile of every slot (27,743 slots). Its input is still unknown:
  not the DXBC checksum, nor MD5/SHA-1/SHA-256/BLAKE2/SHA3 of the blob,
  its DXBC, or its shader code.
- `header.nPermutations` is a format constant, not the slot count: 40 in
  all 430 `0x1000E` DX50 files, 28 in all five `0x1000C` files.

### Block header (96 bytes, 24 little-endian `u32`)

Checked over all 9,742 `0x1000E` DX50 blocks.

| Index | Content | Holds on |
|---|---|---|
| 0 | 3 | all |
| 1 | DXBC size + 56 | all |
| 2 | 40 | all |
| 5 | output register count | 9,693 |
| 6 | instruction count | 7,657 exact |
| 7, 8, 9 | unknown small counts | — |
| 10 | 3 | all |
| 12 | UAV count (0 outside compute) | 9,688 |
| 14–15 | texture binding mask (`u64`) | 9,364 |
| 16 | 0 except in compute; possibly a UAV mask | unverified |
| 18–19 | sampler binding mask (`u64`) | all |
| 20–21 | constant-buffer binding mask (`u64`) | all |
| 22 | DXBC size | all |
| 23 | 4 | all |
| 3, 4, 11, 13, 17 | 0 | all |

None of these fields encode which features a slot index selects.

## GXSH `0x1000C`

Header is the wiki's v1.4 shape (`magic, version, permutationCount,
nShaders, ofsCompressedChunks, nCompressedChunks, ofsCompressedData`),
chunk table and data as above. The 20 bytes between the header and the
chunk table don't read as the wiki's `nShaders` offsets (e.g. `shadowrt`:
`nShaders` 2, bytes `0, 0x25e8, 0x25e8, 0x7a0, 0x2d88`), so there's no
decoded slot table. The exporter finds blobs by walking the inflated
stream for DXBC containers.

All five `0x1000C` files sit under `dx_5_0`, but their blobs are DXIL
(`DXIL` part, SM6 wave ops/barycentrics), not SM5 bytecode.

## DX50 / DX60 pairing

Every `dx_5_0`/`dx_6_0` pair of the same name that has a slot table (430
pairs) has exactly the same set of compiled slots. So a slot index names
the same permutation on both APIs. The number of distinct blobs can
differ (e.g. `terrainlightmap` 34 vs 102), because each API merges
identical programs separately. 17 shaders exist only under `dx_6_0`.

## GFAT `0x1000B`

The wiki's `char numAPIs` + 3 padding bytes reads the same as one `uint32`
on every file (padding always 0). Each entry's `startOffset`/`endOffset`
is file-absolute and bounds an ordinary GXSH `0x1000E` file.

## Disassembly coverage

- vkd3d-compiler 2.0 disassembles every SM5 (TPF) blob, to both D3D asm
  and SPIR-V (9,742/9,742).
- It lowers only 2,741 of 10,247 DXIL blobs to SPIR-V. Most of the rest
  fail with `E8016: DXIL semantic kind 28 is unhandled` (barycentrics);
  the `0x1000C` compute shaders fail on wave ops. `dxc -dumpbin` reads
  every DXIL blob.
