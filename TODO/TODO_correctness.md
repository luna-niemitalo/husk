# TODO: correctness &amp; usability gaps

**Status: an open punch list, not a historical record.** Fixed items get
removed outright rather than kept as `[Fixed]` noise — git history is where
the record of what was fixed and when lives, not a checked-in file.

---

## Read-pipeline correctness

### 1. Cameras (`M2Camera`) — low priority, explicitly deprioritized

`M2Camera` records are WoW's own *baked, model-relative* cinematic camera
paths (character-select rotating camera, some cutscenes, login-screen
framing) — fixed viewpoints authored for Blizzard's own UI/cinematic
contexts, not something a model needs to render correctly from an
arbitrary camera a custom engine already owns. Unlike billboarding,
nothing about normal model rendering depends on this data. Currently
count/offset-only in `husk info`; leave as-is unless the goal ever expands
to literally reproducing WoW's specific character-select/cinematic
screens.

### 2. `.bone` correction-set selection — resolved; application semantics tracked separately

Real DB2 chain confirmed and wired into `husk export --db2-dir/--dbd-dir/
--customization-choice-ids` (`src/chrcustomization_db2.hpp`/`.cpp`):
`ChrCustomizationChoiceID` → `ChrCustomizationElement.
ChrCustomizationBoneSetID` → `ChrCustomizationBoneSet.BoneFileDataID` — a
real customization choice resolves to a real `.bone` FileDataID and, when
that file was also given via `--bones-dir`, marks the matching
`CorrectionSet` with `selected_by_choice_ids` extras. Verified against
real local data (`README.md`'s `.bone` section has usage).

`ChrCustomizationOption`/`ChrCustomizationChoice` (needed to enumerate
real choices by name or pick a default automatically) is still 0 bytes in
the current local extraction — the caller must supply a real choice ID
directly, same as `--char-layout-id` requires for
`CharComponentTextureLayoutsID`.

**Genuinely open, tracked separately**: applying the resolved correction
matrix to real Blender rendering — not a format wall
(`tools/husk_blender_geoset_mask.py` already applies other husk extras to
real rendering elsewhere), but the matrix's own application semantics
(multiply order, local-vs-model space) were never verified against real
client behavior. See `TODO/BONE_CORRECTION_APPLICATION_TODO.md`.

### 3. `tools/live_gallery`'s three.js viewer — curve playback not yet visually confirmed in a real browser

`static/viewer.html`/`.js` plays back both real skeletal animation
(`THREE.AnimationMixer`, clip dropdown, play/pause/loop) and husk's own
extras-driven texture-transform/tint/fade curves (a real JS port of
`tools/husk_blender_geoset_mask.py`'s curve-eval logic), plus mesh/
material picking (click to inspect `blend_mode`/`pixel_shader`/
`vertex_shader`/etc.) and lighting-intensity/exposure sliders. Verified
structurally (element-ID wiring, extras JSON shape cross-checked against
`gltf_mesh.cpp`'s actual output, served correctly through a real local
server) — **not yet visually confirmed in an actual browser** (no headless
browser available in this environment); worth a real look before treating
the curve-playback math as trusted the same way the Blender-side port
already is.

Not attempted, real "overlay shenanigans" stretch scope if there's
appetite for more: JS-side parity for the Blender interactive script's
geoset-switch dropdown and texture-layout overlay — the viewer can inspect
a mesh's material but can't yet toggle geoset variants or preview the
character-texture-layout compositing rectangles the way
`husk_blender_geoset_mask.py` does.
