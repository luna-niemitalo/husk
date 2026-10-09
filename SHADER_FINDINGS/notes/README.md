# Shader notes

Findings from the exported WoW client shaders in `example_exports/shaders/` (gitignored;
regenerate with `tools/export_shaders.py`, whose `README.md` there describes the export
format). Verified findings are promoted into `WIKI_FINDINGS/` (history §19, §20). All
findings come from reading the `dx_5_0` assembly, plus the type names and the `dx_6_0`-only
shaders in the DXIL listings (`dxil_names.md`). mantlecore keeps a copy with its own paths
in `development/shader_notes/notes/`. Nothing was
checked against the running client.

## Files

| File | Covers | State |
|---|---|---|
| `cbuffers.md` | Constant-buffer slots shared across containers, pixel and vertex | Decoded for the shared slots |
| `combiners.md` | M2 model pixel shaders: the 11-bit combiner key, the base forward pass, the `_depth` key | Decoded; bit 9 unknown |
| `m2_vertex.md` | M2 vertex shaders: pairing with the combiners, interpolators, mixed-radix key | Decoded |
| `terrain.md` | Terrain pixel key (672 slots), base program, vertex shader, related containers | Main key decoded; small containers not |
| `wmo.md` | `uber` (WMO materials 0–24 and the outline effect), its key and vertex shader (UV modes 0–8) | Decoded; material 23 and 24 in detail |
| `material3.md` | The other `material3_*` containers | Mostly decoded |
| `particles.md` | M2 particles, ribbons, GPU particles (draw and compute stages) | Decoded, including the `particleupdate` simulation |
| `model_effects.md` | 512-slot M2 effect shaders (`avatar`, `skin`, `gradientmask`, …) and `procedural` | Decoded at key level, `procedural` including its effect layer |
| `liquids.md` | `procwaterabove` key, base program and parameter blocks (pixel and `procwater` vertex); keys of every other liquid container | `procwaterabove` decoded; other containers at key level |
| `sky.md` | Sky dome, clouds, celestial bodies, sun glare, sun shafts | Decoded |
| `lighting.md` | Light buffer (omni, spot), shadow mask, shadow-map pass and its vertex shaders, prepass list, volume-fog chain | Light buffer, shadow mask and shadow-map vertex shaders decoded; volume fog surveyed |
| `postprocess.md` | Colour, glow, fog, depth of field, anti-aliasing, AO, outlines, utilities | Short passes read; long ones identified by name |
| `world_misc.md` | `detaildoodad`, decals, impostors, projected textures, zone surfaces, tooling | `detaildoodad` structure, `decal` and the `edgedecal` key decoded; the rest surveyed |
| `compute.md` | Texture compositing, GPU skinning, clustered-light build, other compute, the `dx_6_0`-only set | Two decoded, `dx_6_0`-only set identified, the rest surveyed |
| `fog.md` | The fog model shared by all forward shaders: record visibility, colour, two-set mode, froxel volume, field roles | Decoded; field names inferred |
| `m2_draw_constants.md` | Per-draw `cb0` (pixel and vertex) and the scene-light struct `cb8` for the M2 families | Decoded |
| `lighting_model.md` | Forward lighting: light set, hemispheric ambient, local lights, clustered light record and its `_lgt.wdt` source, gamma-2.0 combine, `cb7` light flash, post terms, specular (including `illum`) | Decoded |
| `dxil_names.md` | HLSL type names and layouts from the `dx_6_0` DXIL: constant and structured buffers, vertex formats, third-party libraries, `valar`, ray-traced shadows | Names verified; role links marked |
| `reconciliation.md` | Cross-check with `WIKI_FINDINGS/` and `documentation/`: verified, disagreeing and newly derived facts, combiner formula table, block-header decode, `kek.md` checklist | Complete for the overlapping topics |
| `cb_usage.json` | Per-container constant-buffer reads (output of `scripts/cb_usage.py`; gitignored like every `*.json`, regenerate it) | Data |
| `dxil_types.json` | Every DXIL binding and struct body (output of `scripts/dxil_types.py`; gitignored, regenerate it) | Data |

Each file says which statements come from code and which from names or declarations. Its
"Open" or "not decoded" lines list what is left.

## Shared building blocks

These recur across families. They are described once, in the file named:

- Combiner forward pass: alpha test with MSAA coverage, sphere-map UVs, lighting, `cb7`
  light flash, height fog, froxel volume (`combiners.md`).
- Shared slot bits: clustered lights, shadows, prepass, dither, soft fade,
  discard plus depth, simple lighting, an always-set bit (`combiners.md`, with an
  equivalence column in each family's key table).
- Clustered lights: `cb4` grid, 192-byte light records, 33-uint cluster lists
  (`combiners.md`, bit 2).
- Fog: `cb5` fog records of 14 `vec4`s, selected per draw (`cbuffers.md`).
- Height-based layer blend (`terrain.md`; also `uber` material 23 in `wmo.md`).
- Screen-space normal-edge outline (`blueprint` in `model_effects.md`; `uber` effect 25 in
  `wmo.md`).
- Noise dissolve threshold (`skin` bit 4 and `procedural` in `model_effects.md`).

## Scripts (`../scripts`)

| Script | Use |
|---|---|
| `slot_bits.py <container dir>` | For each slot bit, the declaration and opcode changes between neighbouring slots |
| `cb_usage.py <export root> <out.json>` | Constant-buffer usage table for all `dx_5_0` containers |
| `material_section.py <asm file>` | Prints a forward program's material section, between the MSAA branch and the first lighting read |
| `slot_features.py <container dir>` | One line per slot: blob, instruction count, textures, cbuffers, inputs, outputs (for reading mixed-radix keys) |
| `dxil_types.py <export root> <out.json>` | DXIL type names, layouts and bindings of all `dx_6_0` listings |
