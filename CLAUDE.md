# CLAUDE.md — husk

Global rules apply (`~/.claude/CLAUDE.md`). Nix conventions: `.claude/rules/nix.md`.
Read `DESIGN.md` before any structural change.

## Purpose

CLI that reads WoW client files from a local extraction and writes husk's
canonical bundle (`CANONICAL_FORMAT.md`) for MantleCore.

## Status

- **Current**: per-feature state is `README.md` "Implementation" (the source
  of truth; not restated here). In short:
  - Canonical bundle export: M2 models (`export`), WMOs
    (`export-wmo`), ADT tiles and whole maps (`export-terrain`,
    `export-map`, `export-world`). Ten document kinds, defined and
    validated by `schema/` (`validate-bundle`), schema 0.4.1.
  - Anatomy exchange family (`anatomy`, `anatomy_run`, `anatomy_result`):
    schemas and `validate-bundle` checks only, family version 0.1.0; husk
    writes none of them (`REFACTOR/ANATOMY_EXCHANGE.md`).
  - Corpus store: `store-add`, `mount`, `export-corpus`, `snapshot`.
  - DB2 tools: `db2-info`, `db2-export`, `db2-build`; `info`,
    `dump-chunks`, `resolve` for inspection.
  - Blender world authoring addon (`blender/addons/husk_addon`, MVP).
  - glTF: only `export --glb`, a lean projection of the model bundle
    (mesh, skin, animation, base-colour materials; `src/writers/gltf_lean.*`).
- **Target**: the bundle as MantleCore's input (`CANONICAL_FORMAT.md`,
  `REFACTOR/BUNDLE_FORMAT.md`). World completeness in order: WMO + world
  animations → attachments/lights/emitters/fog and particle volumes →
  liquids → culling domains/LOD; model detail (overlays, UV animation)
  alongside (`WORLD_COMPLETENESS.md`). Stage plan: `REFACTOR/README.md`.
- Anything not listed under Current does not exist yet. Do not describe it
  as working.

## Boundaries

- Model file bytes (`.m2`) — chunk container + fixed-offset header/arrays
  (`src/chunk.cpp`, `src/m2.cpp`).
- `.skin` sidecar — triangle-index lookup, submesh/batch structure (`src/skin.cpp`).
- `.skel` sidecar — external bones + sequences (`src/skel.cpp`).
- `.bone` sidecar — per-bone correction matrices, reverse-engineered (`src/bone.cpp`).
- `.anim` sidecar — external per-sequence keyframe blob; `AFM2` (flat) and `AFSB`
  (`.skel`-linked models' real shape) both resolved (`m2::extractAnimBlob`).
- `--textures`/`--skin-dir`/`--anim` directories — user-populated,
  FileDataID-named, local filesystem only. **Never *live* CASC** — husk
  never talks to CASC/DB2 at runtime or depends on the CASC tool itself, by
  design (see `DESIGN.md`'s Non-goals). A local, optional, user-supplied
  `community-listfile.csv`-style snapshot (`--listfile`, `src/listfile.hpp`/
  `.cpp`) is the same "already on disk, never live CASC" tier as every
  other sidecar here — used only as a FileDataID <-> path lookup.
- `.db2` files (real WDC5 container, `src/db2.hpp`/`.cpp`), real column
  names via an optional local WoWDBDefs checkout (`src/dbd.hpp`/`.cpp`),
  a generic named-column reader on top of both (`src/db2table.hpp`/`.cpp`),
  and typed readers on top of that (`src/*_db2.hpp`/`.cpp`) — locally
  extracted files only, same "user-populated, never CASC" tier.
- World files (`src/adt.cpp`, `src/wmo.cpp`): ADT root `<map>_<x>_<y>.adt`
  and its `_obj0`/`_obj1`/`_tex0`/`_lod` siblings, the map `.wdt` and its
  `_fogs`/`_mpv`/`_lgt`/`_occ` sidecars, WMO root and group files — same
  local, user-extracted tier. Name-table paths (`MMDX`/`MWMO`/`MTEX`) are
  resolved through `--listfile` only (`sources::fileDataIdsForGamePaths`).
- `.blp` texture files — decoded in-process (`src/blp.cpp`); the separate
  `blp/` Python tool (Pillow block decode) is its test oracle.
- No network access anywhere in this tool. No user input beyond CLI argv
  and the TOML config file (`src/cli_args.*`, each `src/cmd_*.cpp`; no
  interactive prompts).

Every boundary above is read via explicit bounds-checked parsing at named offsets,
throwing a descriptive `ParseError`/`std::runtime_error` on anything foreign data
claims that doesn't fit — never a silent misread (see git history for the specific
bugs found and fixed under this discipline), and `WIKI_FINDINGS.md` for every
real-file-driven spec correction found along the way.

## Resume

A snapshot, overwritten each session. History: `CLAUDE_HISTORY.md` (newest
first; the pre-2026-11-09 dated entries of this section are archived there
verbatim).

- **Last state (2026-11-09)**:
  - WoW Forever (`wowf` 1.60.1, a preview of retail 12.1.7) surveyed and
    decoded: `WIKI_FINDINGS/FOREVER.md`. In the bundle, optional:
    `resources.terrain.liquid_flow` (WDT `MAI2`) and
    `liquid_types[].waves` (FFTTile).
  - Listfile: the mmap cache has a path index (format 2);
    `tools/listfile_index.py` reads it from Python; `--listfile <dir>`
    means the newest `*.csv` in it (snapshots in the gitignored
    `development/listfiles/`).
  - Docs consolidated: README design/implementation split, DESIGN.md
    trimmed (canon designs in `REFACTOR/CANON_DESIGN.md`), root cleanup,
    stale claims fixed.
  - `~/docs` conformance pass done: flake named sets and ccache path, this
    file in the template's shape, five files split under 1000 lines,
    history/`TODO: Remove` comments swept (remainder: `STYLE_CLEANUP.md`
    item 3), IN/OUT contract annotations in every parser. Larger boundary
    gaps it found are `STYLE_CLEANUP.md` item 4.
  - Legacy glTF pipeline deleted (bdec5ce..1e4b766): `export` writes the
    model bundle; its glTF extras moved into the bundle (schema 0.3.0);
    `--glb` is the only glTF; docs, comments and help text updated to
    match (through d82fdac). export-world's model pass still passed the
    removed `--bundle-only` and failed every run without `--skip-models`;
    fixed with a test that runs the model pass (fb09000).
  - 2026-11-24, DB2 and gear:
    - DB2 reads WDC5 copy-table rows and always sign-extends
      BitpackedSigned (a939391): tables gain rows (ComponentModelFileData
      65 → 59,539; ItemDisplayInfo +108k), and output built before it
      lacks them. db2-export and db2-info take string and float columns
      from the WoWDBDefs layout (08a9b01, f555015; `db2-info --dbd-dir`).
    - Material layers carry `color_alpha` and `texture_weight` (967587e).
    - Gear: every item model and texture is the wearer's race/sex file
      (`src/component_file_db2.hpp`, f0e7c18). The bundle side is schema
      0.4.0: branch `format04-items` (08dbcd0), merged into `format-0.4`,
      worktree removed.
  - 2026-11-24: playwright removed from the flake; `casc-tool` input
    commented out of the dev shell (Luna, b13f953).
  - 2026-11-25: **schema 0.4.0 is on master** (Luna's merge 5540b08 of
    `format-0.4`): 1187/1187 with real data, lint 0 problems, x-source
    required (`bundle_schema::kSourceRequired`). Release notes:
    `REFACTOR/MANTLECORE_MIGRATION.md` v0.4.0; mantlecore got the
    combined reply (their `feedback/2026-11-24-235309-from-husk-…`).
    All `format04-*`/`mantlecore-*` worktrees removed, branches kept; no
    husk worktrees open.
    - Garments (`REFACTOR/GARMENTS.md`, evidence
      `WIKI_FINDINGS/GARMENTS.md`): body hem 1102 and item-model hem
      submeshes move to the tunic, leggings keep their whole texture
      (Luna); layer/order from WoW's slot draw priority (rule
      `source_slot_priority`, grade ref wow.export, client unverified);
      NPC item slot 11 draws nothing → no garment. Not built: dress vs
      robe, `hides` pieces (HelmetGeosetData), the shared external
      garments list.
    - 0.4.1 (a5f80c3, additive): shader-23 WMO materials carry
      `vertex_tint.color` = MOMT `sidnColor` (rule `shader23_tint_sidn`,
      grade inferred: Luna 2026-11-25, no capture ever, best-evidenced
      guess; housing exteriors are not dyed, `WIKI_FINDINGS/WORLD.md`).
    - Zone fog `extinction` is grade ref (WebWowViewerCpp's 0.0005);
      verifying it needs a disassembler or a screenshot check.
    - Scratch: `/media/luna/work/cache/husk_garments/` (db.sqlite ~1 GB;
      delete when garments work pauses; `peddler/` = Luna's set-3540
      test export).
    - Known bug: the lean glb's skin attributes repeat joint indices per
      vertex (~3M gltf_validator errors, ACCESSOR_JOINTS_INDEX_DUPLICATE).
    - Documents carry no `provenance.producer` (merged `manifest-no-producer`,
      still 0.4.1): the tree's `producer.txt` and the build's `meta.json`
      name it, so unchanged manifests dedup across husk revisions (each
      revision used to add ~58 GB of manifests).
    - **Store (2026-11-25)**: one build, 12.1.5.70961/87b44ae (schema
      0.4.1, tag `live`, no `previous`), 1,803,721 files, 89 GB on disk
      (was ~690 GB). Its documents were stripped of `producer` once
      (`/media/luna/work/cache/husk_snapshot/strip_producer.py`), so later
      exports dedup their manifests against it. All 7 pre-0.4 builds and
      their view trees are gone (Luna: 0.4 is the new base). Logs:
      `snapshot.log`, `after_snapshot.log` there.
  - 2026-11-25: **anatomy exchange format** (mantlecore's ask, Luna: husk
    owns it): `REFACTOR/ANATOMY_EXCHANGE.md`, `schema/anatomy*.schema.json`,
    `src/bundle_schema_anatomy.cpp`, `tests/test_bundle_schema_anatomy.cpp`.
    Luna's calls: In + Out with the MS-Human-700 MJCF referenced as a
    payload, never copied; the family versions alone (0.1.0), WoW kinds
    stay 0.4.1. mantlecore and blender_base_model_bs were told where it
    lives. No producer writes it yet (mantlecore MS7 is the first).
- **Next step** (pick one; each names where its detail lives):
  - mantlecore's open asks (2026-11-24 inbox, ingested):
    `TODO/MANTLECORE_ASKS_TODO.md`.
  - MantleCore migration to the current format:
    `REFACTOR/MANTLECORE_MIGRATION.md` (placement sets, tile-local frames,
    world documents, textures by index).
  - Effects design calls (need Luna): zone fog as a medium (a medium is one
    value, zone fog varies by time of day; `LightDataGlobalVolumeFog` feeds
    it), M2 particle light roles, Phase 5 classification
    (`REFACTOR/EFFECTS.md`).
  - Forever data not yet in the bundle: `MALP`/`MASD` water fields,
    `LightDataGlobalVolumeFog`, the area `.dat` terrain, `ChrModelAltVariant*`
    (`WIKI_FINDINGS/FOREVER.md`).
  - Rebuild the store: `/media/luna/canonical_export` is schema 0.2.1 and
    no longer validates.
  - Blender world authoring: Luna's GUI pass, then the splat bake
    (`REFACTOR/BLENDER_WORLD_AUTHORING.md` "Current state").
  - Per-feature punch lists: `TODO/WORLD/*` (ground-cover scatter places
    nothing on grassy quads, south-edge seam cracks, placement yaw sign,
    LOD bands, lighting, liquid, fog volumes), `TODO/CREATURE_OPTIONS_TODO.md`,
    `TODO/INVESTIGATIONS_TODO.md`.
  - Re-measure ground-effect surface coverage with copy rows read
    (`WIKI_FINDINGS/WORLD.md` "Surface types").
  - Gear for a known class: class-specific item files (ClassID 6, 12) are
    never picked (`REFACTOR/CHARACTER_RENDERING.md` §6 on `format-0.4`).
- **Hazards** (standing facts, not narrative — see `CLAUDE_HISTORY.md` for
  the sessions that established each one):
  - **Hardware facts live outside this file**: crashes, RAM faults and GPU
    behaviour on fox_nest are in `/media/luna/data/rvii_documentation/`
    (canonical, per `~/.claude/CLAUDE.md`); check there, not here or in
    memory. Blender renders with EEVEE (`tests/terrain_bundle_import_check.py
    --render`, `tools/render_lean_glb.py`) are GPU runs and follow
    `~/.claude/rules/gpu-lock.md`; the test suite's Blender step only
    imports (`--background`, no render) and needs no lock.
  - **A worktree without `test_data` silently skips ~90 real-data
    tests**: `test_data/` is gitignored and lives only in the main
    checkout, so a fresh worktree reports green with those tests
    skipped. Link it first (`ln -s /home/luna/dev/husk/test_data
    <worktree>/test_data`, gitignored since d0b05b1) and
    compare the case count to the main tree's before trusting a pass.
  - **A stored build needs a clean producer**: a `-dirty` husk version
    (uncommitted changes) makes `store-add` refuse it.
  - Effect documents are written per bundle (`<bundle>/effects/`), not
    shared across an `export-world` tree yet.
  - Possible bug, unchecked: `db2-export` reads `LightData.Time` as
    65536 + t (like the packed-column bug cbd6e38 fixed, on a u16 column).
  - **Corpus location (2026-10-10, Luna)**: the extracted corpus moves
    from `/media/luna/data/wow_export` (gone; `wow_export_old`/
    `wow_export_superseded` beside it are not the current one) to
    `/media/luna/wow_export` (game files under `live/`), a FUSE mount
    from the CASC work that makes source-data updates declarative and
    deduplicated. It may be absent. The mount command, flag paths and
    listfile are in `development/REFERENCES.md` (gitignored, machine-
    local); running that mount command is pre-approved whenever the mount
    is missing. `~/.config/husk/config.toml` (husk's own config, in
    scope) points at the mount. Tests, tools and usage docs point there
    too; evidence citations in TODO/findings docs keep the old path on
    purpose (they record where something was verified).
  - **Listfile names can belong to another FileDataID**: e.g.
    `helm_armor_ethereal_d_01_gn_m00.skin` is FDID 6217827, while that
    M2's SFID declares 6217937. Same-basename lookups (skins, fuzzy
    texture pool) can't detect this; the mount's `_by_fdid/<fdid>` can't
    be wrong. Every FDID lookup tries it (`sources::
    storePathForFileDataId`/`localFileForFileDataId`): SFID skins and
    AFID anims before the same-basename names, textures after the
    listfile tier, models/WMO groups/terrain textures after the listfile
    path.
  - `tools/corpus_scan_tasks/unfillable_texture_task.py` is the one true
    source for "does this file's texture actually resolve". Any
    reimplementation of husk's texture resolution must mirror all three
    real tiers (literal → `--listfile` → fuzzy same-basename,
    `src/sources/texture_resolution.cpp`) — dropping one is a real,
    previously-hit bug class, not a safe simplification.
  - `src/sources/texture_resolution.hpp`/`.cpp` (`husk::sources`) owns every
    texture-candidate-resolution helper — check there first for
    texture-resolution doc/comment citations.
  - DB2 output built before cbd6e38 (2026-10-11) is stale wherever a
    column is narrower than 32 bits and packed (pallet/common data): husk
    kept the cell's junk high bits (`LiquidObject.LiquidTypeID` 5 read as
    0x3F000005). That covers `knowledge.sqlite` and any older `db2-export`
    database; rebuild before trusting them.
  - Any DB2 code that checks `section.header.tactKeyHash != 0` directly to
    decide whether a section is readable is checking the wrong thing —
    use `db2::Section::recordsAvailable()` (`db2.hpp`) instead. A nonzero
    `tactKeyHash` doesn't mean husk's bytes are still ciphertext; a real
    CASC extraction usually already decrypts these before the file reaches
    disk. husk has no TACT key store and deliberately never will.
  - `/media/luna/work/cache/husk/knowledge.sqlite` (`husk db2-build`'s
    output) contains real but incomplete object-skin data — same-slot
    cross-item collisions are common. Don't pass `--knowledge-db` to `husk
    export` for real output; see `STYLE_CLEANUP.md` item 2 and
    `TODO/TEXTURE_POOL_RECALL_TODO.md` for the disabled subsystem's own
    status and the disambiguation design that would need to land first.
  - `/media/luna/data/wow_export/buildinfo.json` (`clientBuild: 61621`, an
    11.x build) is stale: the mount's `live` is 12.1.5.70961 and `forever`
    1.60.1.71037 (`builds/`; `.listfile` names
    community-listfile-2026-11-21.csv). Date the
    corpus by DB2 layouts, never by `buildinfo.json`.
