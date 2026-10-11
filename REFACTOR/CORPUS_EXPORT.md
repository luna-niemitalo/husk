# CORPUS_EXPORT.md — the whole corpus in canonical format (proposal, 2026-10-11)

**Status: proposal, nothing built.** How a full canonical export of a game
build is laid out, stored across builds, and browsed. Versioning copies
casc-tool's build history store (`~/dev/casc-tool/DESIGN.md`) on purpose:
the same layout, the same invariants, and the same mount serve the
game files and their canonical form.

## Where

| | path | like casc-tool's |
|---|---|---|
| store | `/media/luna/data/canonical_storage/` | `/media/luna/data/wow_storage/` |
| mount | `/media/luna/canonical_export/` | `/media/luna/wow_export/` |

husk writes only under the store (its output dir). The mount is
read-only; nothing writes through it.

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

```
<store>/
  objects/ab/cd/abcd…          one output file's bytes, name = md5 of the bytes
  builds/<label>/
    files.tsv                  path  md5   every file of the tree (a casc-tool path build)
    inputs.tsv                 path  input_fdid  input_md5   what each bundle was built from
    missing.tsv                source_fdid  reason           skipped or failed, and why
    meta.json                  label, game build, husk revision, schema_version,
                               listfile md5, flags, counts, export time
  tags/<name>                  one line: the label (live, ptr, beta, …)
  tmp/                         staging; safe to empty when idle
  logs/                        long runs' output
```

`files.tsv` + `objects/` is exactly casc-tool's *path build*
(`import` without `--root`), so `casc-tool mount` already serves it:

```
/media/luna/canonical_export/live -> builds/<label>
/media/luna/canonical_export/builds/<label>/models/<fdid>.canon.bundle/manifest.json
```

Bundles reference each other and the texture store by relative uri, so
they resolve through the mount unchanged.

### Labels and tags

- **Label**: `<game label>+husk-<short commit>`, e.g.
  `12.1.0.69933+husk-e587735`. The game label comes from the source mount
  (`/media/luna/wow_export/live` -> `builds/12.1.0.69933`), never from
  `buildinfo.json`. A dirty working tree can't produce a labelled build.
- **Tags** mirror the source's: `live` names the newest export of the live
  game build. Re-exporting the same game build with a newer husk moves the
  tag; the older label stays until pruned.

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

## What has to change in husk

- **Determinism (invariant 4).** Documents carry the export time today
  (`writeContractHeader`'s provenance), so identical content hashes
  differently every run. The time moves to `meta.json`. Any absolute
  source path or other run-dependent value in `provenance.sources` goes the
  same way.
- **Input tracking.** Every read through `sources::` records the file's
  FileDataID and md5 for `inputs.tsv` (the md5 of a source file is its CKey,
  so it lines up with casc-tool's manifests). DB2 rows count as inputs of
  the table file.
- **Store writing.** Either husk writes `objects/` + `files.tsv` itself, or
  it exports a tree to the work pool and runs `casc-tool import <store>
  <tree> --label <label> --move`, reusing casc-tool's verified ingestion.
  Recommended: the import, until a reason to duplicate it appears.
  `inputs.tsv`, `missing.tsv` and `meta.json` are husk's either way.
- **`export-corpus`**: the driver. Passes: models, WMOs, maps, then
  `index/` and `corpus.json`; textures go into the store as each pass
  reaches them. Resumable like `export-world`.
- **Shared rig and materials** before the first full run: `humanmale_hd`
  alone is 315 MB, 94 MB of it a rig other models share.

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
dedups shared bytes. It doesn't on `tank/data` (compression and dedup are
off, `~/dev/casc-tool/DESIGN.md` "Problem"). Bundles stay loose files in a
tree; the tree is now a view of a content-addressed store.

## Open (Luna)

- **Dataset.** Creating it is a system mutation, yours to run. Suggested,
  like `wow_storage`: `zfs create -o compression=zstd -o recordsize=1M
  tank/data/canonical_storage` (JSON compresses well; DDS less).
- **Mount point.** `/media/luna/canonical_export` must exist (empty) before
  the first mount.
- **casc-tool `mount` requires a listfile** even when every build is a path
  build, which never reads it. Workaround: pass the same listfile; a
  change belongs in casc-tool.
- **Label shape.** `+husk-<commit>` is a proposal.
- The full run is human-gated: a seeded ~4,000-file sample first, to
  project size and time.
