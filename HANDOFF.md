# Handoff: garment evidence (branch format04-garment-evidence, off format-0.4)

Worktree: /home/luna/dev/husk/.claude/worktrees/format04-garment-evidence (test_data linked).
Findings, with numbers and citations: `WIKI_FINDINGS/GARMENTS.md`. Design status: `REFACTOR/GARMENTS.md`
"Evidence still needed" (rewritten as settled/open).

## Built
- `src/garment_footprint.{hpp,cpp}`: rasterize UV triangles onto the composite grid (pixel centres, edges
  inclusive), geoset triangles from a .skin, bounds / countInside / overlapCount / unite.
- `husk uv-footprint <body.m2> --layout-id N [--geoset id]... [--group g]... [--png-dir D]
  [--texture x.blp --texture-section S]` (`src/cmd_uv_footprint.cpp`, wired in main.cpp/commands.hpp, README).
  Prints layout, sections, per-geoset pixels/UV bbox/model-space bounds/sections, pairwise overlaps.
  Note: with no --group/--geoset it reports every geoset and prints O(n^2) overlap lines (huge).
- `tests/test_garment_footprint.cpp`: 4 cases (synthetic triangles), pass.

## Answers (12.1.5)
- Regions: all playable 2048x1024 layouts share SectionType 0-7 (LegUpper 512,384,512,256; LegLower 512,640,512,256).
- Group 13 on every playable body = 1301 (thigh variant) + 1302/1303 full-length skirts. Skirt footprint
  contains the thigh footprint (humanmale_hd: 1104 ∩ 1302 = 128,512 = all of 1104). No separate hem mesh
  on any body, HD, legacy, or DF+ (dracthyr, earthen same groups as humanmale_hd). Variants nested, not equal.
  1303 is on 15 races incl. legacy-updated goblin, absent on tauren/troll/draenei/worgen/pandaren/...
  and on pre-HD files; no item GeosetGroup value selects 1303.
- Slot table: verified structurally for shirt/chest/robe/waist/legs/feet/hands/back/tabard; head,
  shoulder and -1 values unverifiable; shirt[1]=3 (1 display) and chest[0]=3 (1) contradicted.
- Counts: chest LegUpper overlays 1,520 of 5,339 displays, 1,490 of them skirted (30 tunics);
  legs with skirt 246 of 4,504; chest own-model share rises at Legion (41%), not DF; 200/200 sampled
  legs textures non-empty in the footprint (footprint can't find hems).
- Contradiction to report: stage 2 ("hem mask = hem mesh UV footprint") has no hem mesh to use.

## Open / next step
- Find where a modern tunic hem lives: read the chest items' own models (1,096 chest displays with a
  model and no skirt/leg overlay; ModelResourcesID_0 -> ModelFileData -> .m2): geosets, UVs, which
  texture they sample, and AttachmentGeosetGroup.
- Visual confirmation of 1302 vs 1303 shapes (Blender --background look, or an inbox card for Luna).
- What shows 1303 (ChrCustomizationGeoset GeosetType 13 rows?).

## Hazards
- Scratch data: /media/luna/work/cache/husk_garments/ (db.sqlite ~ full DB2 export with added indexes and
  tables listfile/item_display/gg/disp/disp_facts; *.sql, *.nu; png/). db.sqlite is large; delete when done.
- sqlite3 `-init file` runs before `.output`; use `.output` then `.read` to redirect.
- Legacy bodies were rasterized on layout 103's grid; only UV relations are meaningful there, not pixels.
- Not merged into format-0.4 (by instruction).
