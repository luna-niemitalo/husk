# CORPUS_EXPORT.md — the whole corpus in canonical format (2026-10-11)

**Status: the store and its mount are built** (`husk store-add`, `husk
mount`, `src/corpus_store.*`, `src/cmd_store.cpp`), and documents are
deterministic. Not built: input tracking (`inputs.tsv`), `export-corpus`,
`index/`/`corpus.json`, shared effects/materials/rig, `husk view`; open
items in `TODO/CORPUS_EXPORT_TODO.md`. Today a stored build is an
`export-world` tree. How a full canonical export of a game build is laid
out, stored across builds, and browsed. Versioning follows
casc-tool's build history store (`~/dev/casc-tool/DESIGN.md`): objects
named by their md5, a build as a list of paths to objects, tags naming
builds, the same invariants. husk owns its store and mount outright and
never depends on casc-tool being installed or built; any code taken from
it is copied in, not linked.

## Where

| | path | like casc-tool's |
|---|---|---|
| store | `/media/luna/data/canonical_storage/` | `/media/luna/data/wow_storage/` |
| mount | `/media/luna/canonical_export/` | `/media/luna/wow_export/` |

Both are plain directories (the store sits on `tank/data` beside
`wow_storage`, no dataset of its own). husk writes only under the store
(its output dir). The mount is read-only; nothing writes through it.

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
index/                              derived lookup tables, rebuildable from the tree:
  creature_displays.json            CreatureDisplayInfo id -> (model fdid, preset)
  npcs.json, items.json, chr_models.json, placements_by_model.json
corpus.json                         what this tree covers, counts, coverage
```

- A file's bundle is named by its FileDataID. Creature options, NPC presets
  and character customization stay on the model they belong to; `index/`
  is how a game id (an NPC, a display) finds them.
- A texture nothing references still goes into `textures/` and is listed in
  `corpus.json`. Sound, UI and other unrelated files are out of scope.
- An unsolvable source is skipped and counted (`missing.tsv` below).

## The store

An export has two version axes, the game build and the husk revision, so
builds nest by both:

```
<store>/
  objects/ab/cd/abcd…          one output file's bytes, name = md5 of the bytes
  builds/<game>/<husk>/        e.g. builds/12.1.0.69933/e587735/
    files.tsv                  path  md5                     every file of the tree
    inputs.tsv                 path  input_fdid  input_md5   what each bundle was built from (not built)
    missing.tsv                phase  item  reason           the tree's errors.log, deduplicated
    meta.json                  game, husk, producer, schema_version, counts,
                               source tree, stored_at
  tags/<name>                  one line: <game>/<husk> (live, ptr, beta, …)
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

### The mount (`husk mount <store> <mnt>`)

```
<mnt>/live -> games/12.1.0.69933/e587735       one per tag
<mnt>/games/<game>/current -> <husk>           newest husk export of that game build
<mnt>/games/<game>/<husk>/models/<fdid>.canon.bundle/manifest.json
<mnt>/games/<game>/<husk>/.build               meta.json
<mnt>/games/<game>/<husk>/.missing             missing.tsv
<mnt>/games/<game>/<husk>/.inputs              inputs.tsv
```

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
- **`husk store-add <store> <tree> --game L | --game-from DIR [--tag T]`.**
  Hashes every file (parallel), stores new contents (staged in `tmp/`,
  re-hashed, renamed read-only), writes `missing.tsv`, `meta.json`, then
  `files.tsv` last. The same build again is a no-op; other files under an
  existing build are refused. `--game-from /media/luna/wow_export/live`
  reads the game label from the source mount's tag link. Measured on the
  garrison export: 12,367 files share 2,821 contents; a second build with
  one more map added 7 objects.
- **`husk mount <store> <mnt>`**, above. Verified: every file read through
  it matches the source tree; bundles validate through it; writes fail
  with EROFS.

## Not built

- **Input tracking.** Every read through `sources::` records the file's
  FileDataID and md5 for `inputs.tsv` (the md5 of a source file is its CKey,
  so it lines up with casc-tool's manifests). DB2 rows count as inputs of
  the table file.
- **`export-corpus`**: the driver. Passes: models, WMOs, maps, then
  `index/` and `corpus.json`; textures go into the tree as each pass
  reaches them. Resumable like `export-world`; ends with `store-add`.
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
