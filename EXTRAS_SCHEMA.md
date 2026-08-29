# husk's glTF `extras` schema — index

Every `extras` key `husk export` writes into its output `.glb`, where it
lives, what it carries, and which feature produces it. Without this file the
only way to answer "what extras does husk emit" is to read every `.cpp` that
touches `tinygltf::Value`. This file is the answer,
kept up to date going forward — same "index, not the receipts" role
`WIKI_FINDINGS.md` plays for wowdev.wiki corrections, except the subject
here is husk's own output format rather than an upstream spec.

**Compatibility.** `schema_version` (below) covers the shape/naming/nesting
of every key in this document — not the husk binary's own version, which
changes independently and more often. It only moves on a change a consumer
must react to: a key renamed, removed, restructured, or reinterpreted.
Adding a new key is additive and does not bump it, matching JSON's own
"unknown keys are ignored" convention every consumer of this data already
relies on (Blender's importer, `tools/husk_blender_geoset_mask.py`,
`husk` itself). Current version: **1** (`kExtrasSchemaVersion`,
`src/gltf.hpp`) — the version this document itself describes.

## Where the version lives

Two places, deliberately, because no single one is both universally
present and Blender-survivable:

| Location | Path | Always present? |
|---|---|---|
| `model.asset.extras.schema_version` | glTF's own spec-correct producer-metadata slot, alongside `asset.generator = "husk"` | Yes — every export has an `asset`, even a skeleton-less mesh-only one that has no skin/root joint to carry anything else in this document |
| Root joint's own node `extras.schema_version` | Mirrors the value above, alongside every key in "Root-joint extras" below | Whenever a skin exists (every character/creature export; not a skeleton-less mesh-only export) |

`model.asset.extras` is the canonical source — a plain glTF/JSON reader
never needs to know husk's own root-joint-carrier convention (see below)
just to find a producer version. The mirror exists because Blender's own
glTF importer does not preserve `asset.extras` on any Blender datablock
(confirmed empirically — see `DESIGN.md`'s "Blender-survivable extras
live on the skin's root joint, not the skin"), the same reason every other
per-model key here also lives on the root joint rather than the
spec-natural location (`skin.extras`, also dropped by Blender). Consumers
going through `tools/husk_blender_geoset_mask.py` read the mirror via
`check_schema_version`/`_root_joint_extras`; anything reading the raw
`.glb`/`.gltf` JSON directly can read either (both are always the same
value by construction — one `kExtrasSchemaVersion` constant, two writes).

**Blender-side enforcement is deliberately soft.** `check_schema_version`
(`tools/husk_blender_geoset_mask.py`) only ever prints a diagnostic line —
it never raises and never blocks a later stage. A hard reject on mismatch
was considered and rejected: every real `.glb` husk has ever exported
before this session has no `schema_version` at all, and every `read_*`
function in that file already treats each of its own keys as
independently optional. A hard gate here would be the one place in an
otherwise fully permissive reader that breaks on an old file — worse than
not checking, since the actual data underneath is still perfectly
readable.

## Root-joint extras

Merged onto the skin's first real root joint's own node `extras` — never
`skin.extras` (Blender drops it; see `DESIGN.md`'s writeup above). Found by
scanning imported bones for whichever one carries `joint_names`
(`_root_joint_extras`, `tools/husk_blender_geoset_mask.py`), since
Blender's post-import bone order does not match the raw glTF joint order.
`joint_names` and `schema_version` are the two unconditional keys — every
other key below is present only when its producing feature was used and
found real data.

| Key | Shape | Produced by |
|---|---|---|
| `joint_names` | array\<string\>, index-aligned with `skin.joints[0..joints.size()-1]` | Unconditional, whenever a skin exists |
| `schema_version` | int | Unconditional, whenever a skin exists (mirrors `model.asset.extras.schema_version`) |
| `bone_correction_sets` | array of `{file_data_id, corrections: [{joint, bone_name, matrix[16]}], selected_by_choice_ids?}` | `--bones-dir` |
| `enabled_geosets` | array of `{choice_id, geoset_id}` | `--db2-dir --dbd-dir --customization-choice-ids` (or auto-derived `--chr-model-id`'s own default choices) |
| `creature_enabled_geosets` | array of `{geoset_index, geoset_value, geoset_id}` | `--db2-dir --dbd-dir --creature-display-id` |
| `ribbon_emitters` | array of `{id, joint, bone_name, position[3]}` | Unconditional whenever the model has `M2Ribbon` records |
| `particle_emitters` | array of `{id, joint, bone_name, position[3]}` | Unconditional whenever the model has `M2Particle` records |
| `physics_bodies` | array of `{joint, bone_name, body_type, ...}` | `--phys` |
| `physics_joints` | array of `{body_a, body_b, frequency_hz, damping_ratio, swing_limit_deg}` | `--phys` |
| `chr_texture_layout` | `{layout_id, ..., texture_layers: [{texture_type, blend_mode, texture_section_type_bit_mask, chr_model_texture_target_id}]}` | `--db2-dir --dbd-dir --char-layout-id` (or auto-derived from `--chr-model-id`) |
| `chr_enabled_materials` | array of `{choice_id, chr_model_texture_target_id, material_resources_id, file_data_id}` | `--db2-dir --dbd-dir --customization-choice-ids`/`--chr-model-id` |
| `chr_customization_options` | array of `{option_id, option_name, option_order_index, category_*?, choices: [{choice_id, choice_name, choice_order_index, geoset_id?, materials: [...]}]}` | `--db2-dir --dbd-dir --chr-model-id` (default `auto`; the *full* real menu, not just resolved choices) |
| `gear_section_overlays` | array of `{slot, item_modified_appearance_id, sections: [{component_section, material_resources_id, file_data_id}]}` | `--appearance` gear entries resolving to an object-skin overlay (case 2) |
| `gear_items` | array of `{slot, item_modified_appearance_id, model_file_data_ids, aux_glb_path?, materials: [{texture_type, material_resources_id, file_data_id}]}` | `--appearance` gear entries resolving to standalone geometry (case 1) |
| `animation_data_names` | object, `{animation_name: real AnimationData.db2 name}` | `--db2-dir --dbd-dir`, only for sequences with a real, non-empty `Name` column (current live client extractions have dropped this column entirely — see `DESIGN.md` — so no real export resolves this today; verified correct via a synthetic fixture) |

`animation_data_names` is set separately (`gltf.cpp`, not
`gltf_skeleton.cpp`'s `skinExtras` block) but merges onto the exact same
root-joint node `extras` object as everything else above. It is easy to
miss when counting this carrier's keys: grepping `gltf_skeleton.cpp`'s
`skinExtras` block alone finds 14 (13 plus `schema_version`) and misses
this one. The real total is 15.

## Material extras

`tinygltf::Material.extras`, one object per glTF material
(`gltf_mesh.cpp`'s `emitMaterial`). Every key is structural — driven by
what the source M2 material/batch actually has, not by any `export`
CLI flag (`--textures` gates whether an *image* gets embedded alongside
some of these, not whether the extras key itself appears).

| Key | Shape | Present when |
|---|---|---|
| `additional_textures` | array of `{file_data_id, tex_coord, texture_index?}` | `textureCount > 1` (a second real texture layer on the batch) |
| `texture_transform` | `{constant, translation[3], rotation[4], scaling[3]}` | The batch has an `M2TextureTransform` |
| `texture_transform_animation` | `{translation?, rotation?, scaling?}`, each an array of `{sequence_index?, keyframes: [{time, value}]}` | The transform is animated (not constant) |
| `texture_type` | int | `Material::textureType != 0` (see `gltf_mesh.hpp`) |
| `blend_mode` | int | Real WoW blend mode `> 2` (outside what glTF `alphaMode` can represent losslessly) |
| `pixel_shader` / `vertex_shader` | string | Cata+ real shader names resolved from `shaderId` |
| `texture_file_data_id` | int | The primary texture's real FileDataID is known |
| `diagnostic_name` | string | Always, once a material is built — the full verbose `batch<N>_mat<M>_tex<T>_...` diagnostic chain, kept for cross-referencing back to the source `.skin` batch/texture index even when `name` itself now prefers a cleaner identity |
| `alternate_textures` | array of `{filename, category?, width?, height?, texture_index?}` | A hardcoded-slot material had more than one same-basename candidate file (no FileDataID to disambiguate by) |
| `tint_animation` | array of `{sequence_index?, keyframes: [{time, value[3]}]}` | The batch has an animated tint curve |
| `fade_animation` | `{alpha?, weight?}`, each an array of `{sequence_index?, keyframes: [{time, value}]}` | The batch has an animated alpha-fade and/or weight-fade curve |

## Primitive extras

`tinygltf::Primitive.extras`, one object per glTF primitive
(`gltf_mesh.cpp`). Present whenever the primitive came from a real `.skin`
submesh (`skinSectionId >= 0` — effectively always for a real mesh export).

| Key | Shape |
|---|---|
| `geoset_id` | int — raw `skinSectionId` |
| `geoset_group` | int — `skinSectionId / 100` |
| `geoset_variant` | int — `skinSectionId % 100` |

## Mesh-node extras

`tinygltf::Node.extras` on a mesh's own node (not a joint node).

| Key | Shape | Present when |
|---|---|---|
| `collision` | `true` | `--collision`, and the model has real `M2` collision-mesh data — tags the synthesized collision-mesh node so a consumer can tell it apart from real render geometry |

## Joint-node extras (non-carrier)

`tinygltf::Node.extras` on an individual joint (bone) node — distinct from
the root-joint carrier above, which merges the *whole* table above onto
one specific joint in addition to whatever it already has here.

| Key | Shape | Present when |
|---|---|---|
| `billboard` | int (`M2CompBone::billboardMode`) | The bone is a real billboard bone |

## Anchor-node extras (Attachment / Event / Light)

`M2Attachment`/`M2Event`/`M2Light` become real child glTF nodes (named
`attachment_<id>`/`event_<identifier>`/`light_<i>`), not skin extras —
a bone-relative position marker has an unambiguous glTF representation
(the node's own `.translation`) that the data classes above don't.
`extras` on these nodes carries only what has no other glTF slot.

| Node kind | Key | Shape | Present when |
|---|---|---|---|
| Attachment | `animate_attached` | array of `{sequence_index?, keyframes: [{time, value}]}` | The attachment has an animated visibility curve |
| Event | `data` | int | Always — `M2Event`'s own opaque per-event payload, exposed raw (no core-glTF slot, no decoded semantics) |
| Light | `type` | int | Always — `M2Light::type` |
| Light | `light_animation` | `{ambient_color?, ambient_intensity?, diffuse_color?, diffuse_intensity?, attenuation_start?, attenuation_end?, visibility?}` | Any of those curves is non-empty |

## Animation-clip extras

`tinygltf::Animation.extras`, one object per glTF animation clip
(`gltf_skeleton.cpp`'s `buildAnimationClips`).

| Key | Shape | Present when |
|---|---|---|
| `sequence_metadata` | `{movespeed, frequency, replay_min, replay_max, blend_time_in, blend_time_out, bounds_min[3], bounds_max[3], bounds_radius, variation_next, alias_next, is_alias, animation_data_name?}` | The clip has real `M2Sequence` metadata (effectively always, for a per-sequence clip) — `animation_data_name` only when `--db2-dir --dbd-dir` resolved a real name |

## Verification

Real export used to check this document against actual output:
`character/bloodelf/female/bloodelffemale_hd.m2`, with `--db2-dir`/
`--dbd-dir`/`--listfile` supplied automatically via this machine's
`~/.config/husk/config.toml`. Confirmed present in the resulting `.glb`
(both raw JSON inspection and a headless Blender round-trip through
`tools/husk_blender_geoset_mask.py`): `model.asset.extras.schema_version`,
and on the root joint — `joint_names`, `schema_version`,
`chr_customization_options`, `chr_enabled_materials`,
`chr_texture_layout`, `enabled_geosets`. Every other key in this document was confirmed by reading the code that
emits it (cited inline above), not by an export exercising it this
session — most already have their own dedicated real-fixture or synthetic
test coverage elsewhere in `tests/` (`test_cli_gear_export.cpp` for
`gear_items`/`gear_section_overlays`, `test_gltf_skeleton.cpp`/
`test_integration_weapons.cpp` for `physics_bodies`/`physics_joints`/
`ribbon_emitters`/`particle_emitters`, `test_gltf_skeleton.cpp`/
`test_integration.cpp` for `bone_correction_sets`, `test_gltf_skeleton.cpp`/
`test_integration_lights.cpp` for the Light anchor keys). Two exceptions,
found while writing this document, are worth flagging directly:
`creature_enabled_geosets` and `animation_data_names` were both real,
shipped features with **no test referencing their literal extras key** —
`tests/test_cli_creature_geosets.cpp` did not exist at all, and
`tests/test_cli_animationdata.cpp` covered only `sequence_metadata`'s
animation names, not the root-joint `animation_data_names` object.

**Closed 2026-08-29**: six tests added across those two files, asserting
each key on the skin's root joint plus the negative "flag not given, key
absent" case.
