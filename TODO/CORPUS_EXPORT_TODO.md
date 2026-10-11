# Corpus export — open items

Design and current state: `REFACTOR/CORPUS_EXPORT.md`.

1. **Per-output DB2 inputs**: the tables loaded up front are the run's
   (`*`), so a DB2 change invalidates everything; attributing rows to the
   outputs that read them would narrow it.
2. **More `index/` tables**: NPCs (`Creature` -> displays), items (once
   they are documents), placements by model.
3. **Unnamed M2s**: the 193 `ModelFileData` models the listfile can't name
   are not in `--all-assets`.
4. **Shared effects, materials, rig** across bundles of one tree.
5. **Clean producer**: the checkout's uncommitted `.gitignore` line makes
   every build `-dirty`, which `store-add` refuses.
6. **`husk view`** (reflinked tree) for tools that can't use FUSE.
7. **Prune**: drop builds, then objects no `files.tsv` lists.
8. **Incremental export** from the previous build's `inputs.tsv` and a game
   build diff.
