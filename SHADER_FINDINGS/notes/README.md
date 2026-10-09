# Shader notes

Findings from the exported WoW client shaders in `example_exports/shaders/` (gitignored;
regenerate with `tools/export_shaders.py`, whose `README.md` there describes the export
format). Verified findings are promoted into `WIKI_FINDINGS/` (history §19). All findings come from reading the `dx_5_0` assembly. Nothing was
checked against the running client.

## Files

| File | Covers | State |
|---|---|---|
| `cbuffers.md` | Constant-buffer slots shared across containers, pixel and vertex | Decoded for the shared slots |
| `combiners.md` | M2 model pixel shaders: the 11-bit combiner key, the base forward pass, the `_depth` key | Decoded; bit 9 unknown |
| `m2_vertex.md` | M2 vertex shaders: pairing with the combiners, interpolators, mixed-radix key | Decoded |
| `terrain.md` | Terrain pixel key (672 slots), base program, vertex shader, related containers | Main key decoded; small containers not |
| `wmo.md` | `uber` (WMO materials 0–24 and the outline effect), its key and vertex shader | Decoded; material 23 and 24 in detail |
| `material3.md` | The other `material3_*` containers | Mostly decoded |
| `particles.md` | M2 particles, ribbons, GPU particles (draw and compute stages) | Decoded except the `particleupdate` internals |
| `model_effects.md` | 512-slot M2 effect shaders (`avatar`, `skin`, `gradientmask`, …) and `procedural` | Decoded at key level; `procedural`'s effect layer partly |
| `liquids.md` | `procwaterabove` key and base program; survey of the other liquids | One container decoded, the rest surveyed |
| `sky.md` | Sky dome, clouds, celestial bodies, sun glare, sun shafts | Decoded |
| `lighting.md` | Light buffer (omni, spot), shadow mask, shadow-map pass, prepass list, volume-fog chain | Light buffer and shadow mask decoded; volume fog surveyed |
| `postprocess.md` | Colour, glow, fog, depth of field, anti-aliasing, AO, outlines, utilities | Short passes read; long ones identified by name |
| `world_misc.md` | `detaildoodad`, decals, impostors, projected textures, zone surfaces, tooling | `detaildoodad` structure decoded; the rest surveyed |
| `compute.md` | Texture compositing, GPU skinning, clustered-light build, other compute | Two decoded, the rest surveyed |
| `cb_usage.json` | Per-container constant-buffer reads (output of `scripts/cb_usage.py`; gitignored like every `*.json`, regenerate it) | Data |

Each file says which statements come from code and which from names or declarations. Its
"Open" or "not decoded" lines list what is left.

## Shared building blocks

These recur across families. They are described once, in the file named:

- Combiner forward pass: alpha test with MSAA coverage, sphere-map UVs, lighting, `cb7`
  light, height fog, froxel volume (`combiners.md`).
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
