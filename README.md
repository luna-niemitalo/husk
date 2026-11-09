---
aliases:
  - README
---
# husk

husk reads World of Warcraft's game files from a local extraction (M2
models and their sidecars, WMO buildings, ADT/WDT terrain and maps, DB2
tables, BLP textures) and writes them as open data: the **canonical husk
bundle**, which MantleCore consumes, and, for M2 models, a legacy glTF 2.0
`.glb`.

husk only reads files already on disk. It never talks to CASC or the
network (`DESIGN.md` "Goal", non-goals). Extract files first, e.g. with
[`casc-tool`](https://github.com/luna-niemitalo/casc-tool).

## Build

Nix flake under `nix/`, loaded by direnv.

```
direnv allow                                                    # once
direnv exec . cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # once
direnv exec . cmake --build build
direnv exec . build/husk-tests                                  # TEST_DESIGN.md
```

The binary is `build/husk`. Shell completions (bash/zsh) are generated
into `completions/` from the real flag definitions.

## Usage

`husk <command> --help` prints every command's full flags and defaults;
the generated tables under [Flag reference](#flag-reference-generated)
are the same text.

| Command | Does |
|---|---|
| **Models** | |
| `info <file.m2> [--json]` | header, record counts, chunk tags, sidecar FileDataIDs, emitter summary |
| `export -i <file.m2> [-o out.glb]` | M2 → legacy `.glb`; `--bundle-only` writes the canonical bundle instead |
| `resolve -i <file.m2>` | texture-resolution ledger as JSON: which tier answered each slot |
| `dump-chunks <file.m2\|.bone\|.phys>` | JSON for data with no glTF slot: Legion+ side chunks, full emitter records, `.bone`, `.phys` |
| **World** | |
| `export-terrain <tile.adt> <out>` | one ADT tile → terrain bundle |
| `export-wmo <root.wmo> <out>` | one WMO → object bundle, doodad sets as placement sets |
| `export-map <map-dir> <out>` | one map's world document: tile grid, map-wide volumes, zones |
| `export-world <maps-root> <out> --listfile F --listfile-root D` | every tile of every map plus every placed model and WMO; parallel, resumable |
| `validate-bundle <path>... [--lint-schema]` | check documents against `schema/` |
| **Corpus store** | |
| `export-corpus <out>` | `export-world --all-assets`, `index-tree`, `store-add` in one run |
| `snapshot` | export the source mount's `live` build into the store, tag it `live` |
| `store-add <tree>` | record an export tree as `builds/<game>/<husk>/` |
| `store-prune [--superseded] [--drop <game>/<husk>] [--apply]` | drop old builds and the objects only they listed (dry run without `--apply`) |
| `mount <mountpoint>` | serve the store read-only; unmount with `fusermount3 -u` |
| `index-tree <tree>` | (re)build `index/creature_displays.tsv`, `index/chr_models.tsv` |
| **Data** | |
| `db2-info <file.db2>` | WDC5 header, layout, sample rows |
| `db2-export <file.db2>\|--dir <dir> <out.sqlite>` | DB2 → SQLite, real column names with `--dbd-dir` |
| `db2-build ... -o <out.sqlite>` | knowledge-base database (disabled subsystem, see `STYLE_CLEANUP.md` item 2) |
| `appearance-string --validate <string>` | validate a `husk-appearance/1` string; resolves `gear` with `--db2-dir`/`--dbd-dir` |
| `blp-export <file.blp> <out.png>` / `--dir <in> <out>` | BLP2 → PNG, mip 0 |
| `--version`, `-V` | build version |

### Inputs

- **Sidecars sit next to the model.** `export` finds `.skin`, `.skel`,
  `.anim`, `.bone`, `.phys` and textures beside the `.m2` by FileDataID
  (`SFID`/`AFID`/`BFID`/`PFID`/`TXID`) or same-basename name, and announces
  each choice on stderr.
- **Flags are three-state**: unset (derive), `none` (don't try), or an
  explicit value (`DESIGN.md` "Three-state resolution, not two").
- **Texture resolution** tries, in order: the M2's embedded filename,
  `<FileDataID>.png|.blp`, the `--listfile` path under `--listfile-root`,
  then the one unclaimed same-basename file (ambiguity is reported, never
  guessed). BLP decodes in memory.
- **DB2-driven data** (`--db2-dir` + `--dbd-dir`) is resolved and attached,
  never applied: customization choices, geosets, `.bone` sets, creature
  defaults, character texture layout. Keys: `EXTRAS_SCHEMA.md` (glTF),
  `REFACTOR/BUNDLE_FORMAT.md` (bundle).

### Config file

TOML defaults for per-machine flags (`dbd-dir`, `db2-dir`, `listfile`,
`listfile-root`, `textures`, `store`, ...). Lookup: `--config <path>` →
`$HUSK_CONFIG` → `$XDG_CONFIG_HOME/husk/config.toml` (else
`~/.config/husk/config.toml`); no file is not an error. An explicit flag
beats config, config beats the built-in default; `none` opts a configured
value back out. Details: `DESIGN.md` "Config-file defaults for `export`".

```toml
dbd-dir = "/home/luna/dev/husk/reference/WoWDBDefs"
db2-dir = "/media/luna/wow_export/live/dbfilesclient"
listfile = "/home/luna/dev/husk/development/listfiles"
listfile-root = "/media/luna/wow_export/live"
```

`--listfile` (flag or config) may name a directory: husk then uses its
alphabetically last `*.csv`. Keep dated snapshots in the gitignored
`development/listfiles/` as `community-listfile-YYYY-MM-DD.csv` and the
newest is always picked. The Python tools follow the same rule
(`tools/listfile_index.py`).

`--listfile` is cached under `$XDG_CACHE_HOME/husk` (`$HUSK_CACHE_DIR`
overrides): `listfile.sorted.bin` and `listfile.tag`, safe to delete
(`DESIGN.md` "Listfile cache").

### Blender

- **Canonical bundles**: the world-authoring addon in
  `blender/addons/husk_addon` (`REFACTOR/BLENDER_WORLD_AUTHORING.md`).
- **Legacy `.glb`**: import with *File > Import > glTF 2.0* at default
  settings — the only path verified end to end (drag-and-drop and "import
  with custom settings" both failed in testing). `tools/
  husk_blender_geoset_mask.py` and `tools/husk_blender_options_panel.py`
  turn the extras into geoset switches, texture switches, animation assets
  and jiggle bones (`TOOLS.md`).

## Design (the target)

What husk should be. Each line points at the document that defines it.

- **Pipeline**: `parse → resolve → canonical → write`, one resolution
  boundary, writers that never re-resolve (`REFACTOR/README.md`).
- **The canonical bundle is the export target**; MantleCore is its
  consumer. glTF and Blender are tertiary; the glTF pipeline is to be
  retired (decided, not built). Concept: `CANONICAL_FORMAT.md`; format
  record: `REFACTOR/BUNDLE_FORMAT.md`.
- **Six document kinds** — `model`, `placement_set`, `world`, `effect`,
  `material`, `technique` — under one contract header. `schema/*.schema.json`
  is the definition (embedded in the binary, checked by `validate-bundle`).
  Schema versions are `x.y.z`: `y` for breaking changes, `z` for additions.
- **Invariants** (`REFACTOR/README.md` "Invariants" I1–I8): no paths in
  `canon::`; one implementation per resolution question; a consumer opens
  only what the manifest names; resolution announces itself; definition ≠
  selection; identity is an ID, a name is decoration with a stated source;
  item identity ≠ slot identity; the payload is preserved, never the
  proprietary container.
- **Boundaries**: local files only, never live CASC, no network. Every
  foreign byte is read through bounds-checked parsing at named offsets and
  fails with expected vs. actual, never a silent misread (`CLAUDE.md`
  "Boundaries"). What husk can't resolve is reported, not fabricated.
- **Documents are deterministic**; a game build's export is stored
  content-addressed across builds (`REFACTOR/CORPUS_EXPORT.md`).

Design notes on the code as it stands (why each decision): `DESIGN.md`.
Wiki corrections found against real files: `WIKI_FINDINGS.md`. Why tests
are transcribed from wowdev.wiki independently of `src/`, so a moving
format fails a test instead of being misread: `TEST_DESIGN.md`.

## Implementation (what exists now)

A feature is done where its implementation matches its design document.
Open items live in the linked TODO files (index: `TODO/README.md`).

| Feature | Design | Implementation state |
|---|---|---|
| M2 parsing (header, geometry, skeleton, `.skel`, animation incl. `.anim` `AFM2`/`AFSB`, aliases, global sequences, materials, emitters, collision, `.bone`, `.phys`) | `M2_COMPLETENESS.md`, `DESIGN.md` | Built. Open: `M2Camera` count only (deprioritized) |
| M2 → canonical bundle (`export --bundle-only`) | `REFACTOR/CANONICAL_MODEL.md`, `REFACTOR/BUNDLE_FORMAT.md` | Mesh, rig + animation, materials, UV maps, LOD levels, attachments/events/lights/emitters, collision. Open: `REFACTOR/AUDIT.md` |
| M2 → legacy glTF (`export`) | `DESIGN.md`, `EXTRAS_SCHEMA.md` | Feature-complete; validator- and Blender-checked (`tests/test_conformance.cpp`). Still the default output of `export`; retirement not started |
| Creature options and presets | `REFACTOR/BUNDLE_FORMAT.md` "Options and presets" | Built from CreatureModelData → CreatureDisplayInfo. Open: `TODO/CREATURE_OPTIONS_TODO.md` |
| Character customization and humanoid NPCs | `REFACTOR/BUNDLE_FORMAT.md`, `REFACTOR/MATERIALS.md` | Bundle: options and geosets, skin composites as material documents (slice 1), choice textures bound to their image inputs; no bone sets, items are refs only. glTF extras: full chain. Open: `TODO/MATERIALS_TODO.md`, `TODO/EQUIPPED_GEAR_RENDER_TODO.md` |
| `.bone` correction application | `TODO/BONE_CORRECTION_APPLICATION_TODO.md` | Parsed and selected by customization choice; never applied (semantics unverified against the client) |
| ADT terrain | `REFACTOR/BUNDLE_FORMAT.md` "Terrain tile bundles" | Heightfield, 0–8 splat layers, ground-cover rules, `MH2O`, placements as two sets, `_lod` levels, `_obj1` distant sets. Open: `TODO/WORLD/ADT_TERRAIN_TODO.md`, `ADT_LOD_TODO.md`, `WORLD_PLACEMENT_TODO.md` |
| WMO | `REFACTOR/PLACEMENT_SETS.md`, `TODO/WORLD/WMO_GEOMETRY_TODO.md` | Root + groups → object bundle: geometry, materials, doodad sets, lights, `MLIQ`, portals, face-flag channels (collision), GFID levels, fog/volumes as effects. Open: skybox parsed not written, UV set per shader assumed, `MOC2`/`MOM3`, `MOBS` |
| Maps and worlds | `REFACTOR/BUNDLE_FORMAT.md`, `TODO/WORLD/WDT_TODO.md` | WDT `MAIN`/`MAID`/`MPHD`, world document (grid, `_fogs.wdt`/`_mpv.wdt` volumes, off-grid WMO, zones). Open: `.wdl`, `.pvdata` |
| Effects (fog, volumes, particles) | `REFACTOR/EFFECTS.md` | Every source exported as a placeholder effect; zone lights; derivation started. Open: classification (Phase 5), zone fog as a medium, M2 particle light roles |
| Liquids | `REFACTOR/BUNDLE_FORMAT.md` | `MH2O`, `MLIQ`, liquid types from DB2. Open: `TODO/WORLD/LIQUID_TODO.md` §1–2 |
| Lighting | `TODO/WORLD/LIGHTING_TODO.md` | WMO `MNLD`/`MOLT` static lights, map `Light` zones with `LightParams`. Open: that TODO |
| Levels of detail | `REFACTOR/BUNDLE_FORMAT.md` "Levels of detail" | M2 `_lodNN` skins, WMO GFID rows, terrain `_lod.adt`. Open: `TODO/WORLD/ADT_LOD_TODO.md`, `MFOB` always empty (`FOG_VOLUMES_TODO.md` §1) |
| Surface types | `WIKI_FINDINGS/WORLD.md` "Surface types" | TerrainType rows from terrain, WMO materials and faces. Open: ~7% of terrain quads resolve |
| Corpus store | `REFACTOR/CORPUS_EXPORT.md` | Store, mount, `snapshot`, `export-corpus`, `store-prune`, `index-tree`. Open: shared effects/materials, incremental export, `TODO/CORPUS_EXPORT_TODO.md` |
| Blender world authoring | `REFACTOR/BLENDER_WORLD_AUTHORING.md` | MVP: import by zone, bake onto any dicing. Open: textures/liquids/LOD on baked tiles |
| Blender model addon | `REFACTOR/BLENDER_ADDON.md` | Not built. `tests/bundle_import_check.py` reads mesh, skeleton, base-colour materials |
| Simulation | `REFACTOR/SIMULATION.md` | Proposal, nothing built |
| DB2 | `DESIGN.md` "DB2 → SQLite" | WDC5 reader, WoWDBDefs naming, typed per-table readers |
| BLP | `DESIGN.md` | Palettized, DXT1/3/5, BC5, BGRA. Not: JPEG (none in the corpus), palette alpha depth 4 |
| M3, PM4/PD4 | `DESIGN.md` non-goals, `TODO/WORLD/PM4_PD4_TODO.md` | Not started |

### Legacy glTF roadmap

The original M2 → Blender plan. All eight stages are done; `src/` comments
cite them by number.

1. Static mesh (Z-up → Y-up is `(X, Y, Z)` → `(X, Z, -Y)`, `DESIGN.md`).
2. Skeleton and skinning.
3. External `.skel` skeleton.
4. BLP → PNG (now in-process, `src/blp.cpp`).
5. Materials: one per `.skin` batch (`DESIGN.md`).
6. Animation: inline, global-sequence, external `.anim`; plus `.bone`,
   emitters and `dump-chunks`.
7. Output hardening: `gltf_validator` and headless Blender in the tests.
8. Multi-LOD export (`--lod <n>|all`).

### Format support matrix (M2 / M3 / WMO / ADT / BLP)

✅ read and exported · 📖 read, diagnostic only (`info`/`dump-chunks`) ·
🚧 partial · ⬜ not started · ⬛ not a concept in this format. M3 is not
started in any row (8 `.m3` files exist in the corpus; chunk survey:
`documentation/wowdev-wiki/md/M3.md`). Per-element depth:
`M2_COMPLETENESS.md`, `WORLD_COMPLETENESS.md`.

| Concept | M2 | WMO | ADT / WDT | BLP |
|---|---|---|---|---|
| Container | ✅ `MD20`/`MD21`; every chunk tag checked against a known list; `WFV1`/`WFV2`/`DPIV`/`AFRA` byte-decoded | ✅ root + group files, reversed tags | ✅ root, `_obj0`, `_tex0`, `_lod`, `_obj1`; WDT | ✅ header, mip table |
| Header | ✅ every wiki header field, `global_flags` bits, `textureCombinerCombos` | ✅ `MOHD` | ✅ WDT `MPHD`, `MAIN`, `MAID` | ✅ |
| Geometry | ✅ vertices + `.skin` indices | ✅ `MOVT`/`MOVI`/`MOVX`/`MOBA` | ✅ `MCVT` heights, holes | ⬛ |
| Normals | ✅ | ✅ `MONR` | ✅ `MCNR` (X, Y, Z on disk) | ⬛ |
| UVs | ✅ both sets | ✅ every `MOTV` | ⬛ computed from position + layer scale | ⬛ |
| Vertex colour | ⬛ (`colors` are per-batch tint) | ✅ `MOCV`; ⬜ `MOC2` | ⬜ `MCCV` | ⬛ |
| Skeleton / skinning | ✅ inline or `.skel`; multi-root forests get a non-joint parent | ⬛ | ⬛ | ⬛ |
| Animation | ✅ inline, global-sequence, `.anim` (`AFM2`, `AFSB`), alias chains | ✅ `MOUV` UV animation | ⬛ | ⬛ |
| LOD | ✅ `SFID` skins, `LDV1` | ✅ GFID rows | ✅ `ML*` family | ✅ mip 0 only |
| Materials | ✅ blend mode, flags, static and animated tint/fade, texture transforms, extra layers, shader names | ✅ `MOMT`, `MOTX`; ⬜ `MOM3` | ✅ `MCLY`, `MCAL` (all three forms), `MTXP`; ⬜ `MTXF` | ⬛ |
| Texture references | ✅ `TXID`, `textures`, `textureCombos` | ✅ | ✅ `MDID`/`MHID`, `MTEX` via listfile | ⬛ |
| Texture pixels | ⬛ | ⬛ | ⬛ | ✅ palette, DXT1/3/5, BC5, BGRA; ⬜ JPEG |
| Collision / physics | ✅ collision mesh (`--collision`), `.phys` anchors; 📖 full `.phys`, `PCOL` | ✅ face flags select collision; ⬜ `MOBN`/`MOBR` BSP | ⬛ | ⬛ |
| Attachments, events, cameras | ✅ attachments, events; 🚧 `M2Camera` count only | ⬛ | ⬛ | ⬛ |
| Lights | ✅ | ✅ `MOLT`, `MNLD`, group `MOLS`/`MOLP` light sets | ✅ map `Light` zones (DB2) | ⬛ |
| Particles / ribbons | ✅ placement anchors; 📖 full records; 📖 `TXAC`/`EXPT`/`RPID`/`GPID`/`PGD1` | ✅ `MPVD` as effects | ⬛ `MCSE` is sound, not particles | ⬛ |
| Fog / volumes | ⬛ | ✅ `MFOG`/`MFED`, `MAVG`/`MAVD`/`MBVD` as effects | ✅ `_fogs.wdt`, `_mpv.wdt`; ⬜ `.pvdata` | ⬛ |
| Liquid | ⬛ | ✅ `MLIQ` | ✅ `MH2O`; ⬜ `MCLQ` (pre-Cata) | ⬛ |
| Portals / culling | ⬛ | ✅ `MOPV`/`MOPT`/`MOPR`/`MOPE`; ⬜ `MOVV`/`MOVB` | ⬛ | ⬛ |
| Placement | ⬛ | ✅ `MODS`/`MODN`/`MODI`/`MODD`/`MDDI` → placement sets | ✅ `MDDF`/`MODF`, `MWDR`/`MWDS` → placement sets | ⬛ |
| Skybox | ⬛ | 🚧 `MOSB`/`MOSI` parsed, not written | ⬛ | ⬛ |
| Sidecars | ✅ `.skin`, `.skel`, `.anim`, `.bone`, `.phys` by FileDataID or same basename | ✅ GFID → group files | ⬜ `.wdl` | ⬛ |

WMO chunks not in any row (`MDAL`/`MOPB`, `MOMX`, `MPB*`, v14-alpha
chunks): dispositions in `WORLD_COMPLETENESS.md`.

### Known gaps

Found on the WoW Forever survey (2026-11-09), present in both live and
forever builds; details in `WIKI_FINDINGS/FOREVER.md`.

- **WMO group chunk `MOBS`** is present in every WMO group sampled; husk
  neither parses nor mentions it.
- **`environments/particulatevolumes/pvdata/*.pvdata`** (85 files; not
  chunked, a serialized blob starting `u16 4, u16 3`) are not read; husk
  reads `_mpv.wdt` only.

Unverified claims, open as TODOs:

- **Legacy glTF attachment nodes may sit at twice their offset**: written
  as the model-space position under their bone, never compared against the
  M2's real position (`TODO/RENDER_QUALITY_TODO.md` §6 has the check).
- **Two gear-resolution results predate the DB2 pallet fix** (706c8b6):
  the 2026-08-21 "verified end to end" gear export and the
  "ModelMaterialResourcesID resolves to unrelated textures" finding both
  need re-running (`TODO/INVESTIGATIONS_TODO.md` #19).

Real source-data defects husk reports instead of guessing (missing `.skin`
FileDataIDs, 0-byte files, `.skin`/batch indices past the model's own
arrays): `corpus_reports/REPORT.md`.

## Tooling

- `tools/` — corpus scans, the headless render pipeline, Blender scripts,
  shader export: `TOOLS.md`, `tools/CORPUS_SCANS.md`. The full-corpus
  render is human-gated: never run it without Luna's go-ahead.
- `blp/` — the Python reference BLP decoder `tests/test_blp.cpp` checks
  against (`TEST_DESIGN.md`).

## Flag reference (generated)

Generated from the CLI11 registrations by `husk --print-flag-docs=<command>`;
paste its output between the markers. `tests/test_cli_flag_docs.cpp` fails
when a table drifts.

<!-- BEGIN GENERATED FLAG TABLE: export -->
<!-- generated by `husk --print-flag-docs=export` -- do not hand-edit; regenerate with that command whenever export's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|
| `--input` | `-i` | the .m2 file to export -- required for single-file mode (--from-list supplies model paths instead); also usable as the first bare positional argument | unset |
| `--output` | `-o` | output .glb path (default: '<model-basename>.glb') -- mutually exclusive with --from-list, which always writes into --output-dir instead; also usable as the last bare positional argument | unset |
| `--from-list` | -- | batch mode: a plain text file of .m2 paths, one per line (blank lines and '#'-prefixed comment lines skipped) -- exports every one of them with the same options given on this command line, opening --listfile once and reusing it across the whole batch rather than reloading it per file, the same '--from-list <ids-file> <out-dir>' shape casc-tool's own 'extract-batch' has. Requires --output-dir instead of --output; one file failing is reported and skipped, not fatal to the rest of the batch -- see the run's own final 'N succeeded, M failed' summary line | unset |
| `--output-dir` | -- | directory to write each --from-list entry's .glb into (named '<model-basename>.glb', or '<parent-dir-name>_<model-basename>.glb' if two entries in the same batch share a basename) -- required alongside --from-list, meaningless without it | unset |
| `--skin` | `-s` | a .skin path, or 'auto' to resolve via the model's own SFID chunk, falling back to a same-basename numbered scan next to the model if that doesn't resolve -- never 'none': a .skin file is the sole source of triangle/submesh/batch data, not optional enrichment | `auto` |
| `--textures` | `-t` | directory of already-converted '<FileDataID>.png' files, raw '<FileDataID>.blp' files (decoded and embedded in-memory, no separate husk-blp step needed), or 'none' to skip embedding images (default: the model's own directory) | unset |
| `--textures-out` | -- | directory to also write each --textures .blp's decoded .png to, mirroring its location under --textures (default: unset -- decoded textures stay in-memory only, embedded straight into the .glb, nothing written to disk) | unset |
| `--explain-textures` | -- | after exporting, print the texture-resolution ledger: for every texture slot, which tier answered it (literal / listfile / fuzzy same-basename pool / knowledge base), which file it resolved to, whether the pool was genuinely ambiguous, and on a miss the reason it failed -- off by default. Use this to diff resolution behavior before vs. after a change, rather than inferring it from the export summary | unset |
| `--debug` | -- | print the full fuzzy/ambiguous-texture-match warning dump: one line per batch, every same-basename candidate filename listed -- off by default, since the same candidates are already embedded as 'alternate_textures' extras on the .glb and the default output instead groups by identical candidate set (one summary line per group). Use this only when you need to see every candidate without opening the .glb's own extras JSON | unset |
| `--export-canon` | -- | also run the in-progress canon:: pipeline (REFACTOR/README.md stage 3) against this same model/skin tier, write its own '<output-basename>.canon.glb' (lean glTF projection) and '<output-basename>.canon.bundle/' (native bundle) alongside the real output, and print a structural deviation report to stderr -- off by default, never affects the real export's own output or exit code on failure. Only covers the resolved --skin tier used when --skin is 'auto' with a single (non-'--lod all') entry -- canon::Model has no LOD concept yet, so an ambiguous multi-tier case is skipped with a note rather than guessed at | unset |
| `--bundle-only` | -- | write only the canon:: native bundle, with the output path as the bundle directory: no legacy .glb, no lean .canon.glb, no deviation report, no legacy animation build. Same single-tier restriction as --export-canon; exits 1 when no bundle could be written | unset |
| `--slim-textures` | -- | write resolved base-color textures as real '<output-dir>/textures/<name>.png' files (named by real FileDataID when known, else the resolved source filename) and reference them via glTF's own external image URI, instead of embedding them in the .glb's binary buffer -- off by default (embed, the existing behavior); produces a much smaller .glb for a real character export, at the cost of the .glb no longer being a single self-contained file (the 'textures/' directory must travel with it). Additional-texture-layer and ambiguous-candidate extras images stay embedded regardless -- see README.md | unset |
| `--skin-dir` | -- | directory 'auto' searches for the SFID-declared '<FileDataID>.skin' file, or 'none' to skip that stage (default: the model's own directory) | unset |
| `--anim` | `-a` | 'auto': inline + global-sequence + best-effort external directory search; 'inline': inline + global-sequence only, no external search; 'none': no animation clips at all (bind pose only); or a directory of '<FileDataID>.anim' files, falling back to '<model-basename><animId>-<subId>.anim' when a FileDataID-named file isn't found | `auto` |
| `--skel` | -- | external .skel path (only relevant for a model with 0 inline bones), or 'none' to never look for one (default: a same-basename '.skel' next to the model, if any) | unset |
| `--lod` | -- | '<n>' or 'all' -- only meaningful when --skin resolves via 'auto' (default: entry 0) | unset |
| `--bones-dir` | -- | directory of already-extracted '<FileDataID>.bone' files (per the model's/'.skel's BFID array), or 'none' to skip (default: the model's own directory) -- attached as inert glTF extras only, never applied to the render | unset |
| `--phys` | -- | external .phys path (per the model's own PFID scalar), or 'none' to never look for one (default: a same-basename '.phys' next to the model, if any) -- a minimal per-body placement anchor is attached as inert glTF extras; the full body/shape/joint record set is available via 'husk dump-chunks' instead | unset |
| `--collision` | -- | include the collision mesh, when present, as real (unskinned) geometry tagged {"collision": true} in glTF extras -- off by default, since Blender's stock importer has no concept of that tag and renders it like any other mesh, and the collision hull is often larger than and visually occludes the real character; the full body/shape/joint record set is also always available via 'husk dump-chunks' | unset |
| `--db2-dir` | -- | directory of real, already-extracted character/creature DB2 files (chrmodelmaterial.db2/charcomponenttexturesections.db2/chrmodeltexturelayer.db2/charcomponenttexturelayouts.db2 for --char-layout-id; chrcustomizationelement.db2/chrcustomizationgeoset.db2/chrcustomizationboneset.db2 for --customization-choice-ids; creaturedisplayinfogeosetdata.db2 for --creature-display-id -- real lowercase casc-tool filenames, same directory serves all three) -- combined with --dbd-dir and one of --char-layout-id/--customization-choice-ids/--creature-display-id to attach real DB2 extras; unset (default) skips these features entirely, same as every other opt-in sidecar. 'none' explicitly opts out even when --config/$HUSK_CONFIG supplies a value -- the one per-invocation override a config default previously had no way to disable | unset |
| `--dbd-dir` | -- | a local WoWDBDefs checkout (github.com/wowdev/WoWDBDefs), used to resolve --db2-dir's real column names -- required alongside --db2-dir/--char-layout-id or --db2-dir/--customization-choice-ids, same role as `husk db2-export`'s own --dbd-dir. 'none' explicitly opts out even when --config/$HUSK_CONFIG supplies a value | unset |
| `--char-layout-id` | -- | a real CharComponentTextureLayoutsID (see `husk db2-export`) to filter --db2-dir's data down to. Optional -- unset (default) auto-derives it from the resolved --chr-model-id's own real ChrModel.CharComponentTextureLayoutID column (same auto&#124;none&#124;<id> derivation --chr-model-id itself already uses; --chr-model-id none disables this too); an explicit value always overrides. Requires --db2-dir/--dbd-dir either way | unset |
| `--customization-choice-ids` | -- | comma-separated real ChrCustomizationChoiceID(s) to resolve against --db2-dir -- attaches each choice's real geoset selection as 'enabled_geosets' skin extras, and marks any matching --bones-dir correction set with 'selected_by_choice_ids'; requires --db2-dir/--dbd-dir too, doesn't filter/apply anything itself, same inert-extras treatment as --char-layout-id | unset |
| `--appearance` | -- | a husk-appearance/1 string (src/appearance_string.hpp) -- an alternative, superset way to drive customization/gear resolution in one flag: its 'cust' field feeds the exact same resolution --customization-choice-ids does, and its 'gear' field resolves each real (SLOT, ItemModifiedAppearanceID) pair (src/itemappearance_db2.hpp) to real equipped-item texture/geometry data, attached as inert 'gear_section_overlays'/'gear_items' skin extras (see TODO/EQUIPPED_GEAR_RENDER_TODO.md) -- husk resolves, never applies. Mutually exclusive with --customization-choice-ids (pick one); requires --db2-dir/--dbd-dir, same as every other DB2-driven enrichment here. 'race'/'sex' fields are informational only -- --chr-model-id's own existing auto-derivation is still what determines the resolved character identity | unset |
| `--chr-model-id` | -- | a real ChrModelID (see ChrModel.db2 / `husk db2-export`), the literal 'auto' to derive one automatically, or 'none' to explicitly disable derivation. Same auto&#124;none&#124;<id> three-state convention as --textures/--skin-dir/--skel -- **unset defaults to 'auto'**: whenever --db2-dir/--dbd-dir are given at all, husk now tries to derive a real character identity and attach its full real customization menu (chr_customization_options) automatically, with no separate flag needed. Two derivation paths, tried in order: (1) primary, if --listfile/--listfile-root resolve this .m2's own real FileDataID -- an exact identity match via CreatureModelData/CreatureDisplayInfo/ChrModel, never ambiguous for a real file (e.g. correctly tells dracthyrmale.m2 apart from dracthyrdragon.m2 even though both are valid 'Dracthyr, male' answers); (2) fallback, only when no FileDataID was found: this .m2's own filename against the real client-side <ClientFileString><"male"&#124;"female">[_hd].m2 convention (exact, case-insensitive match only, never fuzzy) -- can be genuinely ambiguous when a race+sex has more than one real model (e.g. Dracthyr's dragon form). Either path reports and skips rather than guessing when it can't resolve to exactly one real ChrModelID. Either way, once resolved, auto-selects a *sensible default* customization choice per option: the choice with the lowest real OrderIndex under each ChrCustomizationOption belonging to this model, mirroring the character-creation UI's own first-shown option; NOT a client-verified default the way --creature-display-id's geoset selection is (no DB2 table states an explicit player default), husk's own heuristic instead. Ignored when --customization-choice-ids is also given (an explicit pick always wins); requires --db2-dir/--dbd-dir too | unset |
| `--creature-display-id` | -- | a real CreatureDisplayInfoID (see `husk db2-export`) to resolve against --db2-dir's creaturedisplayinfogeosetdata.db2 -- attaches that display's real *default* geoset selection as 'creature_enabled_geosets' skin extras (unlike --customization-choice-ids, this is a true default, not a per-choice pick); husk has no way to derive which display ID applies to a given .m2 model on its own, so this must be supplied directly; requires --db2-dir/--dbd-dir too | unset |
| `--object-skin-texture-id` | -- | a real texture FileDataID to fill into any type=2 (object_skin) texture slot that has no FileDataID of its own -- husk can't derive this on its own (it needs the real ItemDisplayInfo/TextureFileData DB2 chain resolved externally), so it must be supplied directly; unset (default) leaves those slots unresolved, same as before this flag existed | unset |
| `--knowledge-db` | -- | husk's own pre-built knowledge-base SQLite database (`husk db2-build`, TODO/TEXTURE_POOL_RECALL_TODO.md) -- when given, resolves this model's own object-skin texture automatically (via the database's own model->texture mapping, keyed by the model's FileDataID under --listfile-root), instead of requiring --object-skin-texture-id per invocation; --object-skin-texture-id still wins if both are given | unset |
| `--listfile` | -- | a local community-listfile.csv-style snapshot (FileDataID;path per line, github.com/wowdev/wow-listfile) -- last-resort fallback when a FileDataID-named texture slot isn't found as '<FileDataID>.{blp,png}' next to --textures: looks up its real name and tries '<listfile-root>/<real-path>' instead, before falling back to the same-basename fuzzy pool. Optional; unset (default) skips this tier entirely, same as every other opt-in sidecar -- never fetched by husk itself, same tier as --dbd-dir's WoWDBDefs checkout. 'none' explicitly opts out even when --config/$HUSK_CONFIG supplies a value | unset |
| `--listfile-root` | -- | the corpus root --listfile's paths are relative to -- deliberately separate from --textures, since --textures also drives the *directory-local* embedded-filename/same-basename matching tried first, and a real corpus root is typically many directories away from any one model. Only meaningful alongside --listfile; ignored otherwise. Default: --textures itself, for the case where a single directory happens to serve both roles. 'none' explicitly opts out even when --config/$HUSK_CONFIG supplies a value | unset |

<!-- END GENERATED FLAG TABLE: export -->

<!-- BEGIN GENERATED FLAG TABLE: export-terrain -->
<!-- generated by `husk --print-flag-docs=export-terrain` -- do not hand-edit; regenerate with that command whenever export-terrain's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|
| `--obj` | -- | placements file: 'auto' (the tile's sibling <stem>_obj0.adt), 'none', or a path | `auto` |
| `--tex` | -- | texture-layer file: 'auto' (the tile's sibling <stem>_tex0.adt), 'none', or a path | `auto` |
| `--lod` | -- | distant-terrain file (coarser mesh levels): 'auto' (the tile's sibling <stem>_lod.adt), 'none', or a path | `auto` |
| `--obj1` | -- | distant-object file (which placements show from afar): 'auto' (the tile's sibling <stem>_obj1.adt), 'none', or a path | `auto` |
| `--wdt` | -- | map WDT, whose MPHD flags pick the alpha-map format: 'auto' (<map>.wdt beside the tile) or a path. No 'none': without a WDT, texture layers can't be read -- use --tex none | `auto` |
| `--listfile` | -- | community listfile CSV, or 'none': finds texture files by FileDataID, resolves name-table (MMDX/MWMO/MTEX) paths to FileDataIDs, and names placed models | unset |
| `--listfile-root` | -- | directory the listfile's paths are relative to, or 'none' | unset |
| `--db2-dir` | -- | DB2 directory for ground cover and surfaces (GroundEffect*, TerrainType*) and liquids, or 'none' | unset |
| `--dbd-dir` | -- | WoWDBDefs definitions directory, or 'none' | unset |
| `--models-dir` | -- | set placement/ground-effect uris to <dir>/<fdid>.canon.bundle where that bundle exists | unset |
| `--list-models` | -- | print '<fdid>\t<listfile path>' for every model the tile references, then exit | unset |

<!-- END GENERATED FLAG TABLE: export-terrain -->

<!-- BEGIN GENERATED FLAG TABLE: export-world -->
<!-- generated by `husk --print-flag-docs=export-world` -- do not hand-edit; regenerate with that command whenever export-world's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|
| `--map` | -- | only these map directories (repeatable); default every map | unset |
| `--listfile` | -- | community listfile CSV: finds texture and model files by FileDataID, resolves name-table (MMDX/MWMO/MTEX) paths to FileDataIDs | -- (required) |
| `--listfile-root` | -- | directory the listfile's paths are relative to | -- (required) |
| `--db2-dir` | -- | DB2 directory for ground cover, surfaces, liquids, zone lights, volume conditions and creature options, or 'none' | unset |
| `--dbd-dir` | -- | WoWDBDefs definitions directory, or 'none' | unset |
| `--jobs` | -- | worker threads (default: hardware threads) | unset |
| `--skip-models` | -- | don't export model bundles (pass 2); placements link only to bundles already under <output>/models/ | unset |
| `--all-assets` | -- | also export every M2 and root WMO the listfile names, placed or not (export-corpus) | unset |
| `--sample` | -- | with --all-assets: only this many of them, a fixed seeded random choice (size and time projections) | unset |
| `--link-store` | -- | store each finished output in this canonical store and replace it with a hardlink to the object (export-corpus), so the tree takes no space beyond new content | unset |

<!-- END GENERATED FLAG TABLE: export-world -->

<!-- BEGIN GENERATED FLAG TABLE: export-corpus -->
<!-- generated by `husk --print-flag-docs=export-corpus` -- do not hand-edit; regenerate with that command whenever export-corpus's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|
| `--map` | -- | only these map directories (repeatable); default every map | unset |
| `--sample` | -- | only this many listfile assets, a fixed seeded random choice (size and time projections) | unset |
| `--listfile` | -- | community listfile CSV: finds texture and model files by FileDataID, resolves name-table (MMDX/MWMO/MTEX) paths to FileDataIDs | -- (required) |
| `--listfile-root` | -- | directory the listfile's paths are relative to | -- (required) |
| `--db2-dir` | -- | DB2 directory, or 'none' | unset |
| `--dbd-dir` | -- | WoWDBDefs definitions directory, or 'none' | unset |
| `--jobs` | -- | worker threads (default: hardware threads) | unset |
| `--store` | -- | record the tree as a build in this store, or 'none'; each output is linked into the store as it is written, so only new content takes space | unset |
| `--game` | -- | the game build the tree was exported from, e.g. 12.1.0.69933 | unset |
| `--game-from` | -- | read the game build from a source mount's tag link instead, e.g. /media/luna/wow_export/live (default: --listfile-root) | unset |
| `--tag` | -- | point this tag (live, ptr, ...) at the build; repeatable | unset |

<!-- END GENERATED FLAG TABLE: export-corpus -->

<!-- BEGIN GENERATED FLAG TABLE: store-add -->
<!-- generated by `husk --print-flag-docs=store-add` -- do not hand-edit; regenerate with that command whenever store-add's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|
| `--store` | -- | the canonical store directory | -- (required) |
| `--link` | -- | replace each stored file in the tree with a hardlink to its object, so the tree takes no space beyond what the store holds once (same filesystem only) | unset |
| `--game` | -- | the game build the tree was exported from, e.g. 12.1.0.69933 | unset |
| `--game-from` | -- | read the game build from a source mount's tag link instead, e.g. /media/luna/wow_export/live | unset |
| `--tag` | -- | point this tag (live, ptr, ...) at the build; repeatable | unset |

<!-- END GENERATED FLAG TABLE: store-add -->

<!-- BEGIN GENERATED FLAG TABLE: store-prune -->
<!-- generated by `husk --print-flag-docs=store-prune` -- do not hand-edit; regenerate with that command whenever store-prune's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|
| `--store` | -- | the canonical store directory | -- (required) |
| `--drop` | -- | drop this build, <game>/<husk> (repeatable) | unset |
| `--superseded` | -- | drop every build no tag names that is not its game's newest (what a newer husk revision replaced) | unset |
| `--apply` | -- | remove them; without it only the plan is printed | unset |

<!-- END GENERATED FLAG TABLE: store-prune -->

<!-- BEGIN GENERATED FLAG TABLE: snapshot -->
<!-- generated by `husk --print-flag-docs=snapshot` -- do not hand-edit; regenerate with that command whenever snapshot's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|
| `--listfile-root` | -- | the source mount's tag link to snapshot, e.g. /media/luna/wow_export/live: the build it names is exported | -- (required) |
| `--listfile` | -- | community listfile CSV | -- (required) |
| `--dbd-dir` | -- | WoWDBDefs definitions directory, or 'none' | unset |
| `--store` | -- | the canonical store directory | -- (required) |
| `--jobs` | -- | worker threads (default: hardware threads) | unset |
| `--tag` | -- | the tag to point at the snapshot | `live` |
| `--previous-tag` | -- | the tag the replaced build keeps | `previous` |

<!-- END GENERATED FLAG TABLE: snapshot -->

<!-- BEGIN GENERATED FLAG TABLE: mount -->
<!-- generated by `husk --print-flag-docs=mount` -- do not hand-edit; regenerate with that command whenever mount's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|
| `--store` | -- | the canonical store directory | -- (required) |
| `--foreground` | -- | stay attached to the terminal instead of daemonizing | unset |

<!-- END GENERATED FLAG TABLE: mount -->

<!-- BEGIN GENERATED FLAG TABLE: index-tree -->
<!-- generated by `husk --print-flag-docs=index-tree` -- do not hand-edit; regenerate with that command whenever index-tree's flags change. -->

| Flag | Short | Description | Default |
|---|---|---|---|

<!-- END GENERATED FLAG TABLE: index-tree -->
