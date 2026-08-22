# TODO: clean human-readable texture names (Blender image names + slim-export naming)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## The question that started this

Luna asked whether husk can resolve wow_export's verbose material-batch
texture names (`batch1_mat3_tex5_skin_haircolor_shininess_bloodelffemale_hd_
texture_<fileid>.png`) down to something as simple as
`scalpupperhair00_08_hd_<file_id>.png`, using mappings husk already has, so
the Blender tooling can present/embed the clean form instead.

## What's already solved (no change needed)

`_resolve_customization_texture_path` (`tools/husk_blender_geoset_mask.py:1350`)
already globs `*_<file_data_id>.png` — this matches **both** wow_export
naming conventions, since both end in `_<fileid>.png` by construction.
Texture *lookup* already works regardless of which convention a given
wow_export dump used. `--slim-textures` (this session) already writes its
own clean `<FileDataID>.png` for husk's own exported textures — no mess
propagates into a slim `.glb`'s own `textures/` directory.

## The real gap: display naming

Two places still surface an ugly or bare name instead of a human-readable
one, even though husk already has the data to do better:

1. **Blender `Image` datablock names.** `_load_customization_texture_image`
   (`tools/husk_blender_geoset_mask.py:1410`) loads via
   `bpy.data.images.load(path, check_existing=True)` — Blender names the
   datablock from `path`'s own basename, so a wow_export verbose name ends
   up as the visible name in Blender's Outliner/Image list, even though the
   node *label* right next to it already uses a real, clean
   `choice["choice_name"]` (line ~1486).
2. **Slim-textures naming** (`writeSlimTextureFile`, `src/gltf_mesh.cpp`)
   uses bare `<FileDataID>.png` — clean and dedup-safe, but not
   human-readable the way `scalpupperhair00_08_hd_<id>.png` is.

## Data husk already has, not yet wired to either site

- `chr_customization_options` extras (`gltf::Skeleton::CustomizationOption`/
  `CustomizationChoice`, `src/gltf_skeleton.hpp`) already carry real
  `optionName`/`choiceName` per texture-bearing choice — already reaches the
  Blender script, already used as a node label, just not as the image name.
- `--listfile`'s `unordered_map<uint32_t, std::string>` (FileDataID → real
  WoW content path, e.g. `character/bloodelf/scalpupperhair00_08.blp`) is
  loaded and used elsewhere in husk (`findFileDataIdForModelPath`,
  `export_texture_resolution.cpp`'s fallback resolution) but is never
  attached to any extras struct — no `real_path`/`content_name` field
  exists on `CustomizationChoice::Material` or any other texture-FileDataID
  site. This is the actual source of the short, human-content-name form
  Luna gave as the target example (`scalpupperhair00_08`, not a husk- or
  choice-name-derived string).

## What implementing this would need (not started)

1. Thread the already-loaded `--listfile` map into whichever export path
   populates `CustomizationChoice::Material`/other texture-FileDataID
   extras sites, and attach the real content-path basename (stem, no
   extension) alongside the existing `fileDataId` field when the listfile
   resolves one — same "husk resolves, never applies" extras policy as
   every other field in `gltf_skeleton.hpp`.
2. Blender side: `_load_customization_texture_image`'s caller renames the
   loaded `Image` datablock post-load to the new real name when present,
   falling back to `choice_name`/bare FileDataID when the listfile didn't
   cover this texture (no `--listfile` given at export time, or a genuine
   miss) — same tiered-fallback shape `_resolve_customization_texture_path`
   itself already uses.
3. Slim-textures naming: same fallback chain, `<real-content-stem>_<
   FileDataID>.png` when available, else today's bare `<FileDataID>.png`.
   Needs the same listfile-derived field from step 1, threaded through
   `Material::baseColorImageName`'s own resolution instead (a separate call
   site from the customization-choice one above — check both are covered,
   not just the customization path).

## Gate

Independent — no client-side/DBC dependency beyond what `--listfile`
already provides today; a genuine but small follow-up, not investigated
further than this scoping pass.
