# CORPUS_EXPORT.md — the whole corpus in canonical format (2026-10-11)

**Status: the store and its mount are built** (`husk store-add`, `husk
mount`, `src/corpus_store.*`, `src/cmd_store.cpp`), documents are
deterministic, `export-world` records each output's inputs
(`src/input_log.*`), `husk export-corpus` drives a whole build, and
`husk index-tree` writes `index/`. Not built: shared effects/materials/rig,
`husk view`, prune; open items in `TODO/CORPUS_EXPORT_TODO.md`. How a full canonical export of a game build is laid
out, stored across builds, and browsed. Versioning follows
casc-tool's build history store (`~/dev/casc-tool/DESIGN.md`): objects
named by their md5, a build as a list of paths to objects, tags naming
builds, the same invariants. husk owns its store and mount outright and
never depends on casc-tool being installed or built; any code taken from
it is copied in, not linked.

## Where

| | path |
|---|---|
| store | `/media/luna/work/canonical_storage/` |
| mount | `/media/luna/canonical_export/` |

Both are plain directories. The store is on the work pool's SSDs, not
`tank`: objects are named by hash, so a build's files land scattered, and
on `tank`'s spinning raidz1 every cold read of one is a ~20 ms seek
(measured 2026-10-12 on casc-tool's store, which moved to the work pool for
the same reason). husk writes only under the store (its output dir). The
mount is read-only; nothing writes through it.

## The tree one export produces

One export is a function of (game build, husk revision, listfile). Its
tree is what `export-world` writes today, widened to every file:

```
textures/<fdid>.dds                 shared texture store
models/<fdid>.canon.bundle/         every M2, placed or not
wmos/<fdid>.canon.bundle/           every root WMO
maps/<map>/<map>.world.bundle       + tiles
effects/<id>.json                   shared effect bodies
materials/<source>_<id>.json        shared material documents (e.g. layout_103_skin)
index/                              derived lookup tables, rebuildable from the tree (TSV):
  creature_displays.tsv             CreatureDisplayInfo row -> model manifest, preset
  chr_models.tsv                    ChrModel row -> model manifest
inputs.log, errors.log              the run's records; store-add turns them into
                                    inputs.tsv and missing.tsv
```

There is no separate `corpus.json`: it would be neither one of the five
document kinds nor needed, since the stored build's `meta.json` and
`missing.tsv` hold the counts and failures.

- A file's bundle is named by its FileDataID. Creature options, NPC presets
  and character customization stay on the model they belong to; `index/`
  is how a game id (an NPC, a display) finds them.
- A texture nothing references still goes into `textures/` and is listed in
  the store's files. Sound, UI and other unrelated files are out of scope.
- An unsolvable source is skipped and counted (`missing.tsv` below).

## The store

An export has two version axes, the game build and the husk revision, so
builds nest by both:

```
<store>/
  objects/ab/cd/abcd…          one output file's bytes, name = md5 of the bytes
  builds/<game>/<husk>/        e.g. builds/12.1.0.69933/e587735/
    files.tsv                  path  md5                     every file of the tree
    inputs.tsv                 output  input  md5            what each output was built from
    missing.tsv                phase  item  reason           the tree's errors.log, deduplicated
    meta.json                  game, husk, producer, schema_version, counts,
                               source tree, stored_at
  tags/<name>                  one line: <game>/<husk> (live, ptr, beta, …)
  runs/<game>/<husk>           one line: the tree an export-corpus run is still writing
  tmp/                         staging; safe to empty when idle
  logs/                        long runs' output
```

- **`<game>`** comes from the source mount (`/media/luna/wow_export/live`
  -> `builds/12.1.0.69933`), never from `buildinfo.json`.
- **`<husk>`** is the producer version every document in the tree names
  (`git describe` at configure time). `store-add` refuses `-dirty`,
  `unknown`, and a tree whose documents disagree. Hazard: the version is
  fixed when CMake configures, so reconfigure after committing; any
  uncommitted change in the checkout (even `.gitignore`) makes it dirty.
- **Tags** mirror the source's: `live` names the newest husk export of the
  live game build. Re-exporting with a newer husk moves the tag; the older
  build stays until pruned.

### The mount (`husk mount <mnt>`)

```
<mnt>/live -> games/12.1.0.69933/e587735       one per tag
<mnt>/games/<game>/current -> <husk>           newest husk export of that game build
<mnt>/games/<game>/<husk>/models/<fdid>.canon.bundle/manifest.json
<mnt>/games/<game>/<husk>/.build               meta.json
<mnt>/games/<game>/<husk>/.missing             missing.tsv
<mnt>/games/<game>/<husk>/.inputs              inputs.tsv
<mnt>/games/<game>/<husk>.partial/             a run still exporting, read straight from its tree
```

`export-corpus` records its run in `runs/` before exporting (refusing a
dirty build there, not hours later) and removes it once the build is
stored; until then the mount shows the tree as it grows, and hides the
`.partial` as soon as the stored build exists.

Read-only FUSE (libfuse3 from the flake), unprivileged through
`fusermount3`; unmount with `fusermount3 -u <mnt>`. Bundles reference each
other and the texture store by relative uri, so they resolve through the
mount unchanged. A build's tree loads from `files.tsv` on first use and
stays cached (listed builds never change); tags and the build list are
re-read, so a `store-add` beside a running mount shows within a minute. A
`files.tsv` the mount can't parse shows as the build's `.error`. Without
FUSE the tree could be materialized as reflinked files (`husk view`, not
built; block cloning is active on `tank/data`).

### Invariants (casc-tool's, applied here)

1. `objects/<k>` exists ⇒ md5(contents) == k. Written to `tmp/`, hashed,
   renamed into place.
2. Objects are immutable and append-only; removal only by an explicit prune
   (drop builds, then objects no `files.tsv` lists).
3. A build's record is its `files.tsv`; `index/` and `corpus.json` are files
   in the tree derived from it, never a second source of truth.
4. **An output's bytes are a function of its inputs and the husk revision.**
   This is what makes objects dedupe across builds: a model whose M2,
   sidecars, textures and DB2 rows did not change produces the same bytes.

## Built

- **Determinism (invariant 4).** Documents no longer carry an export time;
  `meta.json` has `stored_at`. Verified: `fox.m2` and an `export-world` of
  `goralliancegarrisonlevel1` exported twice are byte-identical (12,371
  files). `provenance.sources[].detail` still names the listfile path,
  stable for one machine's config.
- **`husk store-add <tree> --game L | --game-from DIR [--tag T]`.** The
  store, here and in `mount` and `export-corpus`, is the config key
  `store` (`--store` overrides).
  Hashes every file (parallel), stores new contents (staged in `tmp/`,
  re-hashed, renamed read-only), writes `missing.tsv`, `meta.json`, then
  `files.tsv` last. The same build again is a no-op; other files under an
  existing build are refused. `--game-from /media/luna/wow_export/live`
  reads the game label from the source mount's tag link. Measured on the
  garrison export: 12,367 files share 2,821 contents; a second build with
  one more map added 7 objects.
- **`husk mount <mnt>`**, above. Verified: every file read through
  it matches the source tree; bundles validate through it; writes fail
  with EROFS.

- **Input tracking** (`inputs::`, `export-world` only). Each output (a
  bundle directory, e.g. `models/1.canon.bundle`) runs in a per-thread
  scope that worker threads inherit; every source file it *uses* is
  logged with the md5 of the bytes it read (no second read) to the
  tree's `inputs.log`, which `store-add`
  turns into `inputs.tsv` (last row per output and input wins; outputs
  gone from the tree are dropped). Inputs under `--listfile-root` are
  relative to it, so they read as game paths; an md5 of a game file is
  its CKey, comparable with casc-tool's manifests.
  - Recorded at the read helpers (`readFileBytes`, `adt::readFile`, the
    anim and texture readers, the DB2 cache on every lookup, the
    listfile) and at `SharedTextures` cache hits, so reusing what another
    output read still counts.
  - Output `*` is the run: files used outside any scope, i.e. the DB2
    tables and listfile loaded up front. Every output depends on them.
  - Discovery passes (WDTs, tile and WMO-root reference collection) run in
    discarding scopes; each tile and world names its WDT itself.
  - Not tracked: WoWDBDefs definitions (husk-side reference, versioned
    with husk), and `textures/` files as outputs of their own: a texture
    is attributed to every bundle that uses it.
  - Verified on `goralliancegarrisonlevel1`: 2,791 outputs (2,788 model
    bundles, the tile, the world, the run), 8,927 rows; a doodad lists its
    `.m2`, `.skin` and `.blp`, a WMO its root, groups and textures, the
    tile its WDT, ADT, `_obj0`, `_tex0` and ground textures.

- **`husk export-corpus <out> [--store DIR|none] [--game L|--game-from D] [--tag T]...`**:
  `export-world` over `<listfile-root>/world/maps` with `--all-assets`
  (every listfile `.m2` and root `.wmo`, placed or not: 135,129 and
  14,140 in the 2026-10-08 listfile), then `index-tree`, then, unless
  `--store none`, a `store-add`. The game label is resolved before the export,
  from `--listfile-root`'s tag link by default. `--map` and `--sample N`
  (a fixed seeded choice of the listfile assets) bound a projection run.
- **`husk index-tree <tree>`**: rebuilds `index/` from the model
  manifests, parsing only `selection` and `definition`
  (`humanmale_hd`: 1.5 s, 66 MB, ChrModels 1 and 45, 6,381 displays).

## Not built
- **More `index/` tables**: NPCs (`Creature` rows -> displays), items
  (once items are documents), placements by model.
- **The 193 unnamed `ModelFileData` M2s** (`_unresolved/`): `--all-assets`
  enumerates the listfile only.
- **Shared effects, materials and rig** before the first full run:
  `humanmale_hd` alone is 315 MB, 94 MB of it a rig other models share.
  (The store already dedupes identical files; sharing saves the tree's own
  size and the exporter's time.)

## Incremental export

Storage dedupes without anything below (invariant 4); this saves time.
Given the previous export's `inputs.tsv` and the game build diff
(`casc-tool changes A B`, target, not built yet), only bundles with a
changed input are re-exported; the rest copy their `files.tsv` rows. A
changed DB2 invalidates every bundle that read it, so a patch touching
`CreatureDisplayInfo` re-exports most creatures. Until `changes` exists,
every export is full and costs disk only for its delta.

This also answers `RESOURCE_CATALOG.md`'s open cross-patch reference
index: `inputs.tsv` across builds is that edge log.

## Supersedes

`REFACTOR/README.md`'s "physical storage shape — settled" assumed ZFS
dedups shared bytes. It doesn't on `tank/data` (dedup is off,
`~/dev/casc-tool/DESIGN.md` "Problem"). Bundles stay loose files in a
tree; the tree is now a view of a content-addressed store.

## Open (Luna)

- The full run is human-gated: a seeded ~4,000-file sample first, to
  project size and time.
