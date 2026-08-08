# World feature completeness (WMO + ADT)

A granular, per-concept breakdown of "what needs to be built and how far
could it go," combining **WMO** (static building/prop geometry, plus its
own internal doodad set) and **ADT** (terrain, plus the doodad/WMO
*placement* records that turn a pile of M2/WMO files into a rendered
world). Structured the same way `M2_COMPLETENESS.md` is — same three axes,
same table shape — but scoped to "the world" instead of a single model
format.

**Why WMO and ADT are one file, not two**: they're going to be implemented
together, not because they're similar formats (they aren't — WMO is a
chunked, self-contained mesh container; ADT is a chunked terrain-tile grid
with a much bigger sidecar family) but because they answer one combined
question — *what does the world around an M2 look like, and what places
that M2 into it*. ADT's `MDDF`/`MODF` (doodad/WMO placement) is the whole
reason ADT matters to husk at all beyond terrain geometry, and WMO has the
exact same concern turned inward (`MODS`/`MODN`/`MODI`/`MODD` — a WMO's own
internal doodad set, placing M2s *inside* a building). Any real
implementation session is going to touch both at once — a WMO with no ADT
to place it has no world position, and an ADT with no WMO/M2 placement
resolution is just an empty terrain mesh — so tracking them apart would
just mean flipping between two files constantly.

**This is not a replacement for `README.md`'s [format support
matrix](README.md#format-support-matrix-m2--m3--wmo--adt--blp)** — that
table answers "does husk handle this concept at all, in which format,"
already grouped by feature area with the specific chunk tags named. This
file goes one level deeper for the WMO/ADT slice of that matrix
specifically: for each concept, what's the realistic glTF/Blender-side
target once husk *does* touch it, so a future implementation session
starts with the shape of the problem already scoped instead of
re-deriving it chunk-by-chunk from wowdev.wiki.

## Current state: nothing here is implemented yet

Every row below reads `none` / `none` for Parse/Consumption. That's not a
placeholder — `DESIGN.md`'s Non-goals section is explicit: WMO and ADT are
both "tracked, not started," and no code in `src/` reads a WMO or ADT byte
today. **This file is a target-setting scaffold, written before
implementation on purpose** — per-project convention (see `CLAUDE.md`),
thorough documentation now is cheaper than re-deriving scope piecemeal
during implementation later, one clarifying round-trip at a time. Once
real work starts here, this file gets updated in place (edit the row, same
"kept in sync, don't append a note" discipline `M2_COMPLETENESS.md`
already uses) — it is not a punch list (a `*_TODO.md` file, written when
implementation is actually about to start, is) and not a historical record
(git history is).

Sourced from wowdev.wiki's WMO page (`documentation/wowdev-wiki/md/WMO.md`,
root file + group file + `PortalCulling.md`/`Rendering.md`) and ADT's
`v18` page (`documentation/wowdev-wiki/md/ADT/v18.md`), both already
mirrored locally, plus `.wdt`/`.wdl` (`WDT.md`, `WDL/v18.md`) for the two
map-level sidecar formats ADT depends on.

**Update, 2026-08-01: every row below has now been through one real
investigation pass**, expanding this survey into eleven implementation-
ready companion documents, each independently verified against the real,
already-extracted local corpus (`/media/luna/data/wow_export`: 84,798 real
`.wmo` files, ~270,625 real `.adt` files across split-file variants, 959
`.wdt`, 959 `.wdl`) — not just re-read from the wiki. **`WORLD_COMPLETENESS.md`
itself still reads `none`/`none` everywhere below (nothing is implemented),
but "documented, not verified" no longer applies** — see
`WIKI_FINDINGS.md` §15 for the consolidated list of real corrections found
(chunk-tag byte reversal confirmed universal, several wiki struct errors
fixed, four items promoted out of a reflexive `n/a`, two genuinely
undocumented chunks found), and the eleven `*_TODO.md` files below for the
full per-item implementation plan, C++ data-model sketch, and test plan —
a future session should start there, not re-derive scope from this
document alone:

| Document | Covers |
|---|---|
| `TODO/WORLD/WDT_TODO.md` | `.wdt` map root, tile existence, global single-WMO placement, `_occ`/`_lgt` sidecars, `.wdl` |
| `TODO/WORLD/ADT_TERRAIN_TODO.md` | heightmap/normals/holes, Cata+ split-file structure |
| `TODO/WORLD/ADT_LOD_TODO.md` | Legion+ terrain LOD (`ML*` family) |
| `TODO/WORLD/WMO_GEOMETRY_TODO.md` | WMO root+group split, mesh geometry, materials, skybox |
| `TODO/WORLD/WORLD_PLACEMENT_TODO.md` | `MDDF`/`MODF` + WMO's own internal doodad set — the highest-value item in this whole file |
| `TODO/WORLD/LIQUID_TODO.md` | `MLIQ`/`MH2O`/`MCLQ` |
| `TODO/WORLD/LIGHTING_TODO.md` | `MOLT`, the BfA+ lightset system, dynamic/legacy lights, ADT outdoor lighting |
| `TODO/WORLD/FOG_VOLUMES_TODO.md` | WMO fog/particulate/ambient/box volumes, the `_fogs`/`_mpv` sidecars |
| `TODO/WORLD/COLLISION_CULLING_TODO.md` | WMO BSP collision, convex volumes, cutting planes, portals, ADT terrain collision |
| `TODO/WORLD/WORLD_MISC_METADATA_TODO.md` | gameplay/misc metadata final dispositions |
| `TODO/WORLD/PM4_PD4_TODO.md` | server-side pathing mesh (see the "Related, explicitly out of scope" section below — this is no longer fully out of scope) |

**Recommended implementation order**, reasoned from real-data prevalence
and value, not just document order: (1) `TODO/WORLD/WDT_TODO.md` — nothing else can
locate a real file without it; (2) `TODO/WORLD/ADT_TERRAIN_TODO.md` — the core
terrain mesh; (3) `TODO/WORLD/WORLD_PLACEMENT_TODO.md` — actually populates a world
with M2s/WMOs, the single highest-value item; (4) `TODO/WORLD/WMO_GEOMETRY_TODO.md` +
`TODO/WORLD/COLLISION_CULLING_TODO.md`'s WMO-collision item together (the latter is
near-zero extra design cost once M2's own collision-mesh pipeline is
being reused for WMO groups anyway); (5) `TODO/WORLD/LIQUID_TODO.md` (`MH2O` alone
covers 67% of real ADT tiles); (6) `TODO/WORLD/LIGHTING_TODO.md`; (7) the remainder
(`TODO/WORLD/FOG_VOLUMES_TODO.md`, `TODO/WORLD/ADT_LOD_TODO.md`, `TODO/WORLD/WORLD_MISC_METADATA_TODO.md`,
`TODO/WORLD/PM4_PD4_TODO.md`) in any order, none blocking the others.

## The three axes

Identical definitions to `M2_COMPLETENESS.md`, reworded for a
multi-format, not-yet-started scope:

**Parse depth** — how much of a chunk's byte layout husk actually decodes:

| Value | Meaning |
|---|---|
| `none` | Not read at all |
| `descriptor` | Only a count/offset pair or raw chunk bytes are read — no real records |
| `deref` | Real records dereferenced, but only static (non-animated/non-referential) fields |
| `full` | Every field husk needs for the feature, including any animated tracks or cross-references |

**Consumption** — where the parsed data actually goes:

| Value | Meaning |
|---|---|
| `none` | Parsed into memory, then dropped |
| `diagnostic` | Visible via `husk info`/`dump-chunks`-equivalent; never reaches the exported `.glb` |
| `extras` | Written into the `.glb` as glTF `extras` — present in the file, inert to any renderer that doesn't specifically look for it |
| `native` | A real core-glTF construct (accessor, node, mesh, material field) — renders in Blender with zero extra tooling |

**glTF ceiling** — the honest answer to "what's the best this concept can
become, and why":

| Value | Meaning |
|---|---|
| `native — 100%` | Already there for the data currently available |
| `native — gap remains` | `native` is the right target and is reachable; a specific known gap blocks it today |
| `native-possible, not done` | Nothing about glTF/Blender blocks a real translation; husk just hasn't built it yet |
| `node-possible, unclaimed` | Could become a plain glTF node (empty transform, no core semantic — e.g. a portal boundary, a culling volume) rather than a first-class construct; nobody's built even that |
| `extras-capped, permanent` | No core-glTF or common-Blender-importer slot exists for this concept, full stop |
| `n/a` | Out of scope by deliberate project decision, an infrastructure concern with no renderable shape, or a genuine upstream-spec unknown |

Only `native — 100%` means "done." For this file specifically, expect
almost every row's ceiling to be `native-possible, not done` or
`node-possible, unclaimed` — the point of the column here isn't "how far
did we get" (nowhere, yet) but "how far *can* we get, so scope is
understood before code exists."

---

## World structure & scene composition

The reason this file exists — placing discrete objects (M2s, WMOs) into
world space, at every level that concept appears.

| Feature | Format(s) & chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| Map root / tile existence | `.wdt`: `MPHD`, `MAIN`, `MAID`/`MAI2` (FileDataID variants) | none | none | n/a, infrastructure | which of a map's up to 64×64 ADT tiles actually exist; drives which files to even look for, not a renderable concept itself |
| Global single-WMO map placement | `.wdt`: `MWMO` + `MODF` | none | none | native-possible, not done | the "this whole map is one WMO" case (an instance/dungeon) — same `MODF` record shape as ADT's own WMO placement below, just at map scope instead of per-tile; real-data-verified: `MODF.flags & 0x8` can omit `MWMO` entirely (35/228 real global-WMO `.wdt` files), `nameId` then holding a real FileDataID directly — undocumented on `WDT.md` itself — see `TODO/WORLD/WDT_TODO.md` |
| Doodad (M2) placement onto a tile | ADT: `MDDF` + `MMDX`/`MMID` name tables | none | none | native-possible, not done | position/rotation/scale/FileDataID per instance — the single most important row in this whole file: this is what actually populates a rendered world with M2s. Maps directly to a glTF node with a mesh reference + TRS, once the referenced M2 itself exports |
| WMO placement onto a tile | ADT: `MODF` + `MWMO`/`MWID` name tables | none | none | native-possible, not done | same shape as the doodad case, one level up — places a whole WMO instance (building) into the tile |
| WMO's own internal doodad set | WMO root: `MODS` (doodad sets) + `MODN`/`MODI` (name/FileDataID) + `MODD` (per-instance placement) + `MDDI`/`MWDR`/`MWDS` (Shadowlands+ additional-set activation) | none | none | native-possible, not done | the same placement concept as ADT's `MDDF`, just scoped inside one WMO (furniture inside a building) rather than across a terrain tile; a WMO can have multiple mutually-exclusive doodad sets — **decided, 2026-08-01: export every set as separate, individually-toggleable nodes**, same answer as M2's own geoset selection (see `M2_COMPLETENESS.md`), not a baked-in single choice; real-data-verified: `MODI`'s entry count can exceed `MOHD.nDoodadNames` (14.5% of 5,000 real roots sampled, always ≥ never <, size off the chunk not the header), and `MWDR`/`MWDS`'s two-level "activate additional sets" indirection is verified end-to-end against two real placements of the same WMO — see `TODO/WORLD/WORLD_PLACEMENT_TODO.md` |
| Root+group file split, group resolution | WMO: `MOGN` (names)/`MOGI` (info)/`MOGP` (per-group header, in the group file)/`MOGX`/`GFID` (FileDataID → group file) | none | none | n/a, infrastructure | a WMO root file describes N groups, each a separate file (`GFID`-resolved, same "local-directory FileDataID convention, never CASC" policy husk already uses for M2 sidecars — see `DESIGN.md`'s Non-goals); real-data-verified: `GFID` is row-major (`index = lodTier*nGroups + groupIndex`, zero = no file for that cell), undocumented on the wiki as a formula — see `TODO/WORLD/WMO_GEOMETRY_TODO.md` |
| Cata+ split-file structure | ADT: root + `_obj0`/`_obj1`/`_tex0`/`_tex1`/`_lod` sidecar files; `MHID`/`MDID`/`MWDR`/`MWDS` cross-file FileDataID refs (Legion+); `MCIN` (pre-Cata, in-file chunk index instead) | none | none | n/a, infrastructure | one logical ADT tile is up to 6 files since Cataclysm; real-data-verified: `MCVT`/`MCNR` (heightmap+normals) always stay root-resident even after the split, confirmed on 2,500 sampled files — see `TODO/WORLD/ADT_TERRAIN_TODO.md` |
| Skybox | WMO root: `MOSB` (skybox model name) + `MOSI` (FileDataID variant) | none | none | native-possible, not done | just another M2 reference + scope flag, structurally trivial once M2 placement exists |
| Chunk container / magic / header | WMO: `MVER`, `MOHD`, `MOGP` (group header); ADT: `MVER`, `MHDR` | none | none | n/a, infrastructure | same role as M2's own `MD20`/header row — drives parsing, no renderable shape of its own |

## Terrain geometry (ADT)

| Feature | Chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| Heightmap grid | `MCVT` (9×9 outer + 8×8 inner vertices per `MCNK`, 256 `MCNK`s per tile) | none | none | native-possible, not done | indices are implicit from the fixed grid topology (not stored) — the actual terrain triangle mesh; the single biggest ADT geometry item |
| Normals | `MCNR` (per `MCNK`) | none | none | native-possible, not done | paired 1:1 with `MCVT` |
| Terrain holes | per-`MCNK` flags/hole bitmask (`v18.md`'s Terrain Holes section) | none | none | native-possible, not done | which of the 8×8 inner quads are actually absent — has to be applied at mesh-generation time, not a separate glTF concept |
| Per-tile chunk index (pre-Cata) | `MCIN` | none | none | n/a, infrastructure | superseded structurally by the Cata+ split-file layout above |
| Detail-doodad density/placement hints | `MPTX` (`MCNK` sub-chunk); `MCDD` (Cata?+) | none | none | n/a, unclaimed | ground-clutter (grass/rocks) density, not individually placed M2 instances — a different mechanism from `MDDF` |

### Terrain LOD (Legion+ `ML*` family)

A large, mostly self-contained sub-area — real and reportedly substantial
per `documentation/wowdev-wiki/md/ADTLodImplementation.md` (already
mirrored locally), not researched in depth this pass. One combined row
rather than one per chunk, since none of it has been touched yet and the
internal structure needs its own investigation before a per-chunk
breakdown would mean anything:

| Feature | Chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| Legion+ terrain LOD (heightmap, doodad, and lighting variants) | `MLHD`/`MLVH`/`MLVI`/`MLLL`/`MLND`/`MLSI`/`MLLD`/`MLLN`/`MLLV`/`MLLI`/`MLMD`/`MLMX`/`MLDD`/`MLDX`/`MLDL`/`MLFD`/`MBMB`/`MLMB`/`MLDB`, plus Shadowlands+ `MWDR`/`MWDS` doodad refs | none | none | native-possible, not done | mirrors M2's own multi-LOD story (`husk export --lod`, see `M2_COMPLETENESS.md`) at the terrain-tile scale — full implementation-ready plan now in `TODO/WORLD/ADT_LOD_TODO.md`; real-data-verified: `MLLL`'s LOD-band set is really `{4,8,16,32}` (2,000 real `_lod.adt` files), not `2.0` as `ADTLodImplementation.md`'s own prose could be misread to imply |

## WMO static geometry & materials

| Feature | Chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| Mesh geometry (positions, indices, batches) | group file: `MOVT`/`MOVI`/`MOBA` (material batches); `MORI`/`MORB` (triangle-strip variant) | none | none | native-possible, not done | same shape as M2's own mesh row — direct positions/indices, no bone weights involved (WMO has no skeleton) |
| Normals | group file: `MONR` | none | none | native-possible, not done | |
| UVs | group file: `MOTV` | none | none | native-possible, not done | |
| Auto-generated tangents | group file: `MOTA` | none | none | n/a | wiki: often client-generated for certain shaders, not authored data — same "runtime-computed, not stored" situation as M2's own tangents |
| Vertex colors (baked lighting) | group file: `MOCV`/`MOC2` | none | none | native-possible, not done | genuinely per-vertex here (unlike M2's per-batch tint) — real baked-lighting data, `CMapObjGroup::FixColorVertexAlpha`'s decompiled logic already documented on the wiki page directly |
| Per-face material/collision flags | group file: `MOPY`/`MPY2` | none | none | n/a, mixed | dual-purpose: material index per face (materials, below) plus real collision-relevant flags (see Collision section) |
| Materials | root: `MOMT` (material table) + `MOM3` (v3 override) + `MOTX` (texture filenames) | none | none | native-possible, not done | mirrors M2's own materials row — blend mode/texture references translate the same way `alphaMode`/`doubleSided` already do for M2; real-data-verified: `MOTX`/`MOM3` are zero real hits across 12,867 real root files (a fully FileDataID-mode corpus) — see `TODO/WORLD/WMO_GEOMETRY_TODO.md` |
| Texture UV animation | root: `MOUV` | none | none | extras-capped, permanent | same "no animation-channel target" wall M2's own animated texture-transform row already hit (see `M2_COMPLETENESS.md`) |

## Liquid / water

| Feature | Format & chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| WMO liquid surfaces | group file: `MLIQ` | none | none | native-possible, not done | a real, renderable (if simplified) water-plane mesh — LiquidType.dbc lookup for material/shader identity is an external dependency husk won't have, same non-goal shape as M2's DB2-dependent rows; real-data-verified: 2.1% of 2,000 real WMO group files (`TODO/WORLD/LIQUID_TODO.md`) |
| ADT liquid (modern) | `MH2O` (WotLK+: header/attributes/instances/4 vertex-data cases) | none | none | native-possible, not done | the current, non-legacy liquid representation — `v18.md`'s worked example already covers all 4 cases; real-data-verified: header/instances byte-match the wiki exactly, present in **67.1%** of 4,000 real ADT root tiles — the clear implementation-priority item across all of Liquid/Lighting/Fog, see `TODO/WORLD/LIQUID_TODO.md` |
| ADT liquid (legacy) | `MCLQ` (`MCNK` sub-chunk, pre-Cata) | none | none | n/a, superseded | kept only for reading pre-Cata files; `MH2O` is the real target once support exists at all; real-data-verified: confirmed genuinely absent, 0/4,000 real files — don't implement, see `TODO/WORLD/LIQUID_TODO.md` |

## Lighting

| Feature | Format & chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| WMO point lights | root: `MOLT` + per-group refs `MOLR` | none | none | native-possible, not done | **decided, 2026-08-01: adopt `KHR_lights_punctual`** for point/spot/directional lights — checked directly against `MOLT`'s real fields (`TODO/WORLD/LIGHTING_TODO.md`), known gaps accepted (no ambient-light support in the extension, `MOLT`'s two-radius attenuation doesn't fit the extension's single-`range` model, `SMOLight` has no cone-angle data at all; `type=3`/ambient stays `extras`-only, no target exists) — husk's **second** real glTF extension, not its first (it already declares `KHR_materials_unlit`); real-data-verified: `MOLT` 19.4% of 4,000 real root files present, only 1.7% actually populated, `MOLR` 5.5% of 2,000 real group files |
| WMO lightset system (BfA+) | group file: `MOLS`/`MOLP` + `MLSS`/`MLSP`/`MLSO`/`MLSK` | none | none | native-possible, not done | a layered/conditional lighting system replacing the flat `MOLT` model in newer content — real-data-verified: **all of these are group-level, not root** (a real scoping bug this session's own investigation caught and corrected before trusting any count); genuinely rare in this corpus (`MOLS`/`MLSS` 0.3%, `MOLP` 1.9%, `MLSP` 0.6%, `MLSO`/`MLSK` 0%), see `TODO/WORLD/LIGHTING_TODO.md` |
| WMO dynamic lights | group file: `MNLD` + `MNLR` refs | none | none | native-possible, not done | real-data-verified: `MNLD` 6.6% of 4,000 real root files (**root-level**, unlike its own `MNLR` ref list which is group-level, 34.7% of 2,570 real group files) — see `TODO/WORLD/LIGHTING_TODO.md` |
| WMO legacy lightmaps (v14) | `MOLM`/`MOLD` | none | none | n/a, deprioritized | pre-1.0 format, same low-priority treatment README already gives other v14-alpha-only WMO chunks; confirmed 0/2,570 real group files this session |
| WMO spotlight/pointlight animation (binary-only, not in files) | `MOS2` (spotlight, unknown 108-byte struct), `MOP2` (pointlight, documented struct incl. flicker + `lightTextureFileDataId`) | none | none | extras-capped, permanent (`MOP2`) / n/a (`MOS2`, undocumented) | animated light *properties* — same wall as M2's animated tint/fade; `MOS2`'s struct is flagged unknown on the wiki itself, not just unread by husk; confirmed 0/2,570 real group files for both this session |
| ADT outdoor lighting | none identified in `v18.md` | none | none | n/a, unconfirmed | reportedly zone/`Light.dbc`-driven rather than stored per-ADT — investigated this session (`TODO/WORLD/LIGHTING_TODO.md`), still no per-ADT storage location found; see that doc for the full `DB/Light*.md`/`Rendering/Lighting.md`/`Rendering/DayNight.md` citation trail |

## Fog & atmospheric volumes

| Feature | Format & chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| WMO fog | root: `MFOG` + `MFED` (extra data) + `MFOB` (fog objects) | none | none | node-possible, unclaimed | a volume/falloff description, not geometry — could become a plain marker node at best |
| WMO particulate/ambient/box volumes | group file: `MPVD`/`MAVG`/`MAVD`/`MBVD` + reference lists `MPVR`/`MAVR`/`MBVR`/`MFVR` | none | none | node-possible, unclaimed | same class as M2's own particle emitters (see `M2_COMPLETENESS.md`) — procedural, not renderable geometry; likely the same "minimal anchor + full-data diagnostic dump" split M2's particles/ribbons already established; real-data-verified: all group-level (a real scoping bug this session's own investigation caught before trusting root-level counts), `MPVR` 2.0%/`MAVR` 3.3%/`MBVR` 3.4%/`MFVR` 11.9% of 2,570 real group files; `MPVD`'s own struct remains genuinely unresolved (a 48-byte-stride hypothesis produced an implausible subnormal float) — see `TODO/WORLD/FOG_VOLUMES_TODO.md` |
| Map-level fog volumes | `.wdt` `_fogs` sidecar: `VFOG`/`VFEX` | none | none | node-possible, unclaimed | global per-map fog, distinct from a WMO's own local `MFOG`; real-data-verified: 10.9% of 753 real `_fogs.wdt` files non-empty; **a wholly undocumented chunk, `VFE2` (176 bytes), found in real `_fogs.wdt` files by two independent sibling investigations this session** — real, present, absent from `WDT.md` entirely, not yet reverse-engineered — see `TODO/WORLD/FOG_VOLUMES_TODO.md` and `WIKI_FINDINGS.md` §15 |
| Map-level particulate volumes | `.wdt` `_mpv` sidecar: `PVPD`/`PVMI`/`PVBD` | none | none | node-possible, unclaimed | map-scope counterpart to WMO's own `MPVD`; real-data-verified: `PVMI`/`PVPD`/`PVBD` repeat as a group per volume (5-24 times per file, not once per tag), 6.4% of 753 real `_mpv.wdt` files non-empty, `PVPD`'s two wiki-hedged constant values confirmed exactly — see `TODO/WORLD/FOG_VOLUMES_TODO.md` |

## Collision, culling & visibility

| Feature | Format & chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| WMO collision mesh (BSP) | group file: `MOBN`/`MOBR` | none | none | native-possible, not done | mirrors M2's own collision-mesh row (already `native — 100%` there, see `M2_COMPLETENESS.md`) — same "plain unskinned triangle mesh tagged in `extras`" pattern should transfer directly; **real-data-verified, near-zero new design cost**: present in 34,555/71,929 real group files (48%), zero bounds violations across 4 real files decoded independently once `MOBR`'s one indirection (a triangle index into `MOVI`, not a raw vertex index) is handled — see `TODO/WORLD/COLLISION_CULLING_TODO.md` |
| WMO convex collision volumes | group file: `MCVP` (optional) | none | none | native-possible, not done | |
| WMO terrain-cutting planes | group file: `MOPL` | none | none | node-possible, unclaimed | describes how the WMO cuts into ADT terrain (basements, etc.) — no direct mesh-geometry equivalent |
| WMO portals / visibility culling | root: `MOPV`/`MOPT`/`MOPR`/`MOPE`; group file: `MOVV`/`MOVB` (visible-block lists) | none | none | node-possible, unclaimed | `PortalCulling.md` (already mirrored) documents the runtime algorithm in detail — **checked directly this session, not assumed**: Blender's own real-time engines (Eevee/Eevee Next) implement camera-frustum + occlusion culling, never a cell-and-portal graph; two specifically-named candidates (Cycles' Holdout shader, per-object Ray Visibility toggles) were checked and ruled out for concrete documented reasons — `node-possible, unclaimed` holds, now for a checked reason rather than a repeated assumption; `wow.export` independently arrives at the same "diagnostic JSON, never geometry" shape and has zero `MOBN`/`MOBR` support at all — see `TODO/WORLD/COLLISION_CULLING_TODO.md` and `WIKI_FINDINGS.md` §15 |
| ADT terrain collision | none — presumed to be the render mesh itself | none | none | n/a, unconfirmed | no separate ADT collision chunk found in `v18.md`; carried over from README as an inference — re-checked this session, still no contradicting evidence found, see `TODO/WORLD/COLLISION_CULLING_TODO.md` |

## Gameplay & misc metadata (not independently renderable)

| Feature | Format & chunks | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|---|
| WMO per-face ground type | `MOQG`/`MOGX` (**both group-level** — corrected this session, `WORLD_COMPLETENESS.md` previously mislabeled `MOQG` as root) | none | none | n/a | gameplay-only (footstep sounds, etc.), same class as M2's own bypassed hardware-bone-limit metadata — **reconsidered directly this session under "coverage is the goal, not invisible-means-unimportant" and confirmed to genuinely belong at `n/a`** (no visual/geometric consequence, no existing glTF/Blender convention for audio-FX selection); real-data-verified: `MOQG` 2,219/71,929 group files, and `MOGX` is really a fixed **256-byte** chunk (64×`uint32_t`, only slot 0 populated), not the wiki's stated 4-byte "one single value" — see `TODO/WORLD/WORLD_MISC_METADATA_TODO.md` |
| WMO material/prepass overrides | `MDAL`/`MOPB` | none | none | `MDAL`: **extras** (promoted, was `n/a`) / `MOPB`: `n/a`, investigated | `MDAL` genuinely affects rendered lighting (a per-group ambient-color override, `MOHD.ambColor`'s substitute) — reconsidered and promoted; `MOPB`'s "prepass" naming suggests a render-pipeline-ordering concept, not visual-identity data, investigated and confirmed to stay `n/a` despite being surprisingly common (49% of 71,929 group files) — see `TODO/WORLD/WORLD_MISC_METADATA_TODO.md` |
| WMO unknown-structure chunk | `MOMX` | none | none | n/a, diagnostic | wiki itself: "just a guess" — **investigated from scratch this session rather than left at a shrug** (per this project's own established "unknown on the wiki is a reason to investigate, not to skip" precedent): genuinely common, not rare (3,931/12,869 real root files, 30.5%), and its record count exactly equals `MOHD.nTextures` on every file checked (a new structural fact, not on the wiki) — per-field semantics remain unresolved, see `TODO/WORLD/WORLD_MISC_METADATA_TODO.md` and `WIKI_FINDINGS.md` §15 |
| WMO rare/unclear chunk family | `MPB*` (`MPBV`/`MPBP`/`MPBI`/`MPBG`) | none | none | n/a | wiki: seen in exactly one known alpha file, never read by any shipped client — confirmed corpus-wide this session, 0/71,929 real group files, the cleanest closed item in this section |
| ADT sound emitter placement | `MCNK` sub-chunk: `MCSE` | none | none | **extras** (promoted, was `n/a`) | a real `id + position` shape, structurally identical to M2's own ribbon/particle `EmitterAnchor` precedent — reconsidered this session and promoted to match it exactly (audio playback stays out of scope, placement doesn't); real chunk data itself not yet found populated in this corpus (a targeted follow-up search is needed) — see `TODO/WORLD/WORLD_MISC_METADATA_TODO.md` |
| ADT chunk-level shadow map | `MCNK` sub-chunk: `MCSH` | none | none | **extras / native-possible, not done** (promoted, was `n/a`) | real, decodes to a strongly bimodal shadow-density distribution on real data (confirms genuine baked-lighting content) — reconsidered this session the same way `MOCV` is treated above: doesn't map cleanly onto a per-vertex channel (64×64=4,096 texels vs. `MCVT`'s 145 vertices, a real resolution mismatch), but a small per-`MCNK` texture is a genuine, non-lossy translation once ADT terrain UV export exists — see `TODO/WORLD/WORLD_MISC_METADATA_TODO.md` |
| ADT material override / blend batching | `MCMT` (Cata+), `MCBB` (MoP+) | none | none | `MCMT`: **extras** (promoted, was `n/a`) / `MCBB`: unresolved, needs follow-up | `MCMT` is a real foreign key into `TerrainMaterialRec` driving shader/material selection, not gameplay-only — reconsidered and promoted; `MCBB`'s own presence wasn't confirmed this session (wrong split-file type sampled — it lives in root/`_lod`, not `_tex0`), and its referenced `MBMH`/`MBMI`/`MBNV` chunk family sounds structurally like genuine decorative blend-mesh geometry, not chased down yet — see `TODO/WORLD/WORLD_MISC_METADATA_TODO.md` |

## Sidecar & dependency formats (own byte layouts, not covered by any row above)

Each of these is a real, separate file format that will need its own
investigation (and likely its own small `*_COMPLETENESS.md` section or a
dedicated `*_TODO.md`, per this project's usual per-format lifecycle) once
work actually starts — listed here only so the dependency is visible, not
broken down field-by-field yet:

- **`.wdt`** (`documentation/wowdev-wiki/md/WDT.md`) — per-map root: tile
  existence (`MAIN`/`MPHD`), the global single-WMO-map case (`MWMO`/`MODF`,
  see World structure above), plus four further sidecar-of-a-sidecar file
  variants this same doc covers: `_occ`/`_lgt` (occlusion/lighting probe
  data), `_fogs` (map-level fog, see Fog section above), `_mpv` (map-level
  particulate volumes, see Fog section above). Real-data-verified this
  session (`TODO/WORLD/WDT_TODO.md`): `_occ.wdt`'s `MAOI`/`MAOH` occlusion heightmap is
  really `int16_t[17*17]` (578 bytes), not the wiki's stated `(17*17+16*16)`
  (1,090 bytes) — confirmed across all 959 real files, 43,833 tile entries,
  zero exceptions; a wholly separate, undocumented `_preload.wdt` file
  family was also found and flagged as an out-of-scope pointer, not chased.
  A full implementation-ready plan for all of this now lives in
  `TODO/WORLD/WDT_TODO.md`.
- **`.wdl`** (`documentation/wowdev-wiki/md/WDL/v18.md`) — coarse,
  whole-continent low-resolution heightmap for distant/minimap-scale
  terrain (`MAOF` + a `MapAreaLow` array: `MARE`/`MAOC`/`MAOE`/`MAHO`),
  plus its own reduced-detail doodad/WMO placement for distant silhouettes
  (`MWMO`/`MWID`/`MODF`, `MLDD`/`MLDX`/`MLMD`/`MLMX`) and a skybox-related
  chunk family (`MSSN`/`MSSC`/`MSSO`/`MSSF`/`MSLD`/`MSLI`) not yet
  cross-referenced against WMO's own skybox chunks above. Folded into
  `TODO/WORLD/WDT_TODO.md` as one shared "map-level sidecar formats" document, per
  this file's own framing above.
- **WMO group files** — already folded into the sections above (a group
  file is just where a chunk physically lives, per the root file's own
  `GFID`/`MOGI` table — not a distinct feature on its own).
- **ADT Cata+ split files** (`_obj0`/`_obj1`/`_tex0`/`_tex1`/`_lod`) —
  likewise already folded into the World structure section above.
  Real-data-verified this session: `MCSH`/`MCMT` both live in the `_tex0`
  split file specifically (confirmed by direct decode of
  `2454_55_8_tex0.adt`'s headerless `MCNK` sub-chunk layout, matching the
  wiki's own "header in root, no header in obj and tex" line byte-for-byte)
  — see `TODO/WORLD/WORLD_MISC_METADATA_TODO.md`.

## Related, explicitly out of scope for this file

- **PM4/PD4** (server-side navigation/pathing mesh) — **no longer fully out
  of scope, corrected 2026-08-01 (`LUNA_NOTES.md`)**: even though it's
  never touched by the client renderer, it should still be part of the
  world mesh (hidden by default, not excluded) — the same spirit as this
  project's own M2 collision-mesh precedent (real, exportable, tagged
  inert rather than omitted). A full investigation and implementation plan
  now lives in `TODO/WORLD/PM4_PD4_TODO.md`, with one real structural finding worth
  noting here directly: **PM4/PD4 files are genuinely never shipped to the
  client** (a full `casc-tool list` sweep of 3,190,909 real files in live
  retail storage found zero `.pm4`/`.pd4` matches anywhere) — a confirmed
  structural absence, not an extraction-completeness gap like `EXP2`/`PFDC`
  (`WIKI_FINDINGS.md` §13), so real fixture data for this format will need
  a different acquisition path than every other item in this file.
  **Decided, 2026-08-01**: the "hidden by default" mechanism reuses the
  M2 collision-mesh precedent as-is (a real, `{"pathing": true}`-tagged
  `gltf::NamedMesh`, present and identifiable but not literally hidden in
  Blender's stock importer) rather than glTF's `KHR_node_visibility`
  extension (real, matches the intent, but unsupported by Blender's stock
  importer) or a separate opt-in Blender script — see `TODO/WORLD/PM4_PD4_TODO.md`
  for the full reasoning and the two alternatives kept on record.
- **M3** — WoW's newer model format, structurally much closer to M2 than
  to WMO/ADT (per-vertex data, materials, likely animation) — belongs
  alongside `M2_COMPLETENESS.md`'s own scope if/when it's picked up, not
  here.
- **Shader bytecode** (`BLS`/`GFAT`) — `DESIGN.md`'s Non-goals: investigated,
  deliberately deprioritized. Would matter for WMO/ADT materials
  eventually (real shader-driven blending) but is its own cross-cutting
  concern, not a WMO/ADT-specific row.
