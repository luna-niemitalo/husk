# Handoff: garment evidence (branch format04-garment-evidence, off format-0.4)

Worktree: /home/luna/dev/husk/.claude/worktrees/format04-garment-evidence (test_data linked).
Findings, with numbers and citations: `WIKI_FINDINGS/GARMENTS.md` (sections "Item models" and "What the
evidence supports for the hem split"). Design status: `REFACTOR/GARMENTS.md` "Evidence still needed".

## Built
- `src/garment_footprint.{hpp,cpp}`: rasterize UV triangles onto a pixel grid, geoset triangles from a
  .skin, bounds / countInside / overlapCount / unite. Tests: `tests/test_garment_footprint.cpp`.
- `husk uv-footprint <model.m2> [--layout-id N] [--geoset id]... [--group g]... [--png-dir D]
  [--texture x.blp [--texture-section S]]` (`src/cmd_uv_footprint.cpp`). Per geoset: pixels, UV bbox,
  model-space bounds, `tex=<M2Texture type>/uv<set>` of its batches. Without `--layout-id`: item mode,
  grid = the texture's size, `<model>_<geoset>_on_texture.png` overlays.

## Answers (12.1.5)
- Body: no hem mesh; group-13 body skirts are robe geometry (Luna confirms: a robe hides the thighs).
  1303 is selected by no DB2 row on a playable body; same extent as 1302, more triangles.
- A modern hem is a group-13 (sometimes 11) submesh of the set's item model, on its own texture; no item
  mesh samples the body composite (0/1,687 geosets). 94 chest displays show one without a body skirt,
  291 legs displays show one (289 the same mesh as a chest of the set). 1,002 chest models stop at the
  waist. Slot rule (show group*100+1+AttachmentGeosetGroup[i]) is wow.export's, unverified.

## Open / next step
- Luna picks among the hem-split options (WIKI_FINDINGS/GARMENTS.md last section); then redesign stage 2.
- Visual check of the PNGs: /media/luna/work/cache/husk_garments/png/chest_models/.
- Count which legs models pair with the 1,002 waist-length chest models.

## Hazards
- Scratch data: /media/luna/work/cache/husk_garments/ (db.sqlite ~ full DB2 export + tables listfile,
  item_display, disp, disp_facts, chest_models, cm_files, model_geosets, shown, disp_hem; c*.sql, *.nu;
  png/). db.sqlite is ~1 GB; delete when the design is settled.
- sqlite3 only inside the flake env (`direnv exec <wt> sqlite3`); `-init` runs before `.output`.
- `m2::Model::textureCoordCombos` is empty on every model read (body and item), so `tex=` shows `uv-`;
  whether the files or the loader is unchecked (possible loader bug).
- Legacy bodies were rasterized on layout 103's grid; only UV relations are meaningful there.
- Not merged into format-0.4 (by instruction).
