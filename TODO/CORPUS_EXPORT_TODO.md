# Corpus export — open items

Design and current state: `REFACTOR/CORPUS_EXPORT.md`.

1. **`inputs.tsv`**: record each bundle's source FileDataIDs and md5s
   through `sources::`; DB2 tables as inputs.
2. **`export-corpus`**: every M2 and root WMO from the listfile, every map,
   then `index/` and `corpus.json`; resumable; ends in `store-add`.
3. **`index/`**: creature displays, NPCs, items, ChrModels, placements by
   model.
4. **Shared effects, materials, rig** across bundles of one tree.
5. **Clean producer**: the checkout's uncommitted `.gitignore` line makes
   every build `-dirty`, which `store-add` refuses.
6. **`husk view`** (reflinked tree) for tools that can't use FUSE.
7. **Prune**: drop builds, then objects no `files.tsv` lists.
8. **Incremental export** from the previous build's `inputs.tsv` and a game
   build diff.
