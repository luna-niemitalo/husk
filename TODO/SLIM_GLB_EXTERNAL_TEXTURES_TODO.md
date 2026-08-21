# TODO: slim `.glb` export (external texture references, not embedded)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## The question that started this

Luna asked directly: can `husk export` today produce a `.glb` with zero
embedded textures, that instead reads all its textures from an adjacent
directory in `.png` format? **No — not implemented.** Investigated, not
guessed:

- Every real texture husk resolves ends up in `gltf::Material::baseColorImagePng`
  (`src/export_materials.cpp`), already-decoded PNG bytes in memory.
- `gltf_mesh.cpp`'s material-emission code (`emitMaterial` and the two other
  `tinygltf::Image` construction sites) always does `img.bufferView = imgView`
  (`appendBufferView(buffer, views, mat.baseColorImagePng, ...)`) — every
  resolved texture gets embedded into the `.glb`'s own binary buffer. `img.uri`
  (glTF's real external-reference mechanism) is never set anywhere in this
  codebase.
- No CLI flag exists to opt out of this. `--textures-out <dir>` (already
  real, `src/cmd_export.cpp`) writes a convenience copy of each decoded
  `.blp`'s `.png` to disk — but per its own doc comment (`README.md`),
  "embedding itself always happens in-memory regardless." It doesn't skip
  embedding.

So today: `--textures-out` gets you the PNGs on disk *in addition to* a
fully-embedded `.glb`, not *instead of* one.

## Why this would work (already verified, not a new risk)

Blender's own glTF importer resolves an external `img.uri` relative to the
`.glb`/`.gltf` file's own directory (standard glTF external-resource
resolution) — this is exactly the same relative-path mechanism
(`bpy.path.abspath("//")`, `//`-prefixed `Image.filepath`) already confirmed
directly this session for a different but related fix (`tools/
husk_blender_geoset_mask.py`'s own zero-argument `textures_dir` fallback).
No open question on the Blender-import side — this is purely an export-side
(C++) gap.

## What's needed to implement it

1. **New CLI flag** — e.g. `husk export --slim` or `--external-textures`
   (name not decided; should read naturally next to the existing
   `--textures`/`--textures-out` pair) — threaded through `ExportOptions`/
   `addExportOptions` (`src/commands.hpp`/`cmd_export.cpp`), same pattern
   every other export flag already follows.
2. **Write instead of embed** — wherever `img.bufferView = imgView` happens
   today (three sites in `gltf_mesh.cpp`, all doing the same
   `appendBufferView` dance), branch on the new flag: write
   `mat.baseColorImagePng`'s bytes to a real file instead of appending to
   the in-memory buffer, and set `img.uri` (a relative path) instead of
   `img.bufferView`. The actual disk-write machinery already exists and is
   already tested (`--textures-out`'s own code path) — this is mostly
   *repurposing* that, not building a new writer.
3. **Naming/location convention** — needs a real decision, not a guess:
   - Real per-choice customization textures already use `<FileDataID>.png`
     (`_resolve_customization_texture_path`, Blender side) — `gltf::Material::
     baseColorTextureFileDataId` (`src/gltf_mesh.hpp`) already carries this
     for every material that resolved one, so reusing the exact same
     `<FileDataID>.png` convention for base textures too would mean **one
     naming scheme for every texture kind this project touches**, not a
     second one invented for this feature. Real gap: a material with
     `baseColorTextureFileDataId == 0` (embedded-filename-only source, no
     real FileDataID at all) has no ID to name a file by — needs a fallback
     (`baseColorImageName`, the real source stem, already carried for
     exactly this "no FileDataID" case elsewhere in this same struct).
   - Location: a flat directory next to the `.glb`, or a `textures/`
     subfolder? The Blender-side zero-argument fallback (this session, see
     `CLAUDE_HISTORY.md`) already prefers a `textures/` subfolder next to
     the `.blend` when one exists, falling back to the bare directory
     otherwise — matching that same convention on the export side (write
     into `<output-dir>/textures/` by default) would make a slim export and
     the Blender-side zero-argument workflow compose with no extra flags on
     either end. Worth confirming with Luna before committing to it, since
     it's a real, visible on-disk layout choice, not just an internal
     detail.
4. **Multiple materials sharing one image** — `alternateTextureCache`
   (`gltf_mesh.cpp`) already dedupes embedded images by source filename;
   the same cache should dedupe *written files* too (write once, reference
   by the same URI from every material that shares it), not one copy per
   material.
5. **`gltf_validator`/Blender-import conformance** — `tests/
   test_conformance.cpp` already runs every real export through both
   `gltf_validator` and a headless Blender import; a slim export needs the
   same real verification, not just "looks right" — confirm the validator
   accepts a `.glb` with `img.uri`-only images (no embedded buffer for
   those slots) and that Blender's own importer actually resolves and
   loads them from the adjacent directory, not just that import doesn't
   error.
6. **Interaction with existing `alternate_textures`/multi-candidate extras**
   (`export_materials.cpp`) — these currently embed every ambiguous
   candidate's own bytes as extras (`gltf_mesh.hpp`'s `AlternateTexture`).
   Under slim mode, should these also become external references, or stay
   embedded (they're comparatively rare/small)? Not investigated.

## Real value

Meaningfully smaller `.glb` files for real character exports — this
session's own `bloodelffemale_hd` test case embeds several real texture
atlases directly; a slim export would drop that weight entirely from the
`.glb` and put it in plain, individually-inspectable `.png` files instead
(also easier to diff/replace one texture without re-exporting the whole
model).

## Gate

Independent — no client-side/DBC dependency, no human-judgment call beyond
the naming/location convention question in step 3, which just needs a
quick decision, not a real investigation.
