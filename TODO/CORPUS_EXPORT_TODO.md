# Corpus export — open items

Design and current state: `REFACTOR/CORPUS_EXPORT.md`.

1. **Per-output DB2 inputs**: the tables loaded up front are the run's
   (`*`), so a DB2 change invalidates everything; attributing rows to the
   outputs that read them would narrow it.
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
