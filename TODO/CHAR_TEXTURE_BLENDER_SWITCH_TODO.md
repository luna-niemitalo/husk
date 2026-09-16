# TODO: Blender-side character-texture customization switch (Stage 5)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history (and `CLAUDE_HISTORY.md`'s
2026-09-16 archival entry, condensed from this file's own former "found
it, fixed it, verified it" narration) is the record of what was fixed and
when, not this file.

**Scope: legacy-only.** This Blender tooling consumes the legacy
pipeline's own skin extras (`chr_customization_options`, etc.); `canon::`'s
bundle format will carry the same customization data differently, so this
node-graph machinery will need redoing once the Blender addon (
`REFACTOR/BLENDER_ADDON.md`) replaces it.

## Background

husk resolves real DB2-driven character-customization data (which
`ChrCustomizationOption`/`Choice` a character has, which texture each
choice maps to, and the real placement rects/blend modes each texture
layer needs) but deliberately never composites pixels itself — that's the
wrong layer for it (a real pixel compositor was built and then reverted;
see git history and `DESIGN.md`). `tools/husk_blender_geoset_mask.py` is
where that data becomes a real, live, switchable Blender node graph
instead: one combined `ShaderNodeGroup` per material
(`_build_material_customization_group`), built from nested `MenuSwitch`
groups (one real dropdown per `ChrCustomizationOption`, including a
correct fix for options whose choices depend on another option's own
current choice — e.g. Face's texture depends on which Skin Color is
selected), each choice's own real texture correctly placed and blended
within the shared texture atlas. This is verified end to end structurally
and via headless Cycles evaluation against real DB2-resolved data
(`bloodelffemale_hd`, `nightelffemale_hd`), including real multi-layer
compositing (scars + markings + tattoo simultaneously, independently
toggleable).

## Open items

1. **A real interactive Blender GUI visual pass — human-gated, Luna's own
   task.** No automated pixel-perfect render test exists for this kind of
   feature (same standing discipline every other Blender-side feature in
   this project follows). Needs: open the export in Blender's own GUI with
   *real* per-choice texture bytes (not the synthetic placeholder PNGs
   used for structural verification), confirm changing an option's
   `Choice` dropdown visibly changes the rendered result on the actual
   character mesh, confirm overlays land in the correct UV position (not
   offset/flipped), and confirm blend modes look plausible (multiply
   darkens, screen lightens, etc.). Get Luna's own eyes on a real render
   before considering this feature fully done.

2. **`tools/husk_blender_options_panel.py` can't find a dependent
   option's socket by name.** The options panel looks up a material's
   promoted socket via `node.inputs.get(row.option_name)` (an exact-name
   lookup, per its own doc comment). A *driving-dependent* option's socket
   (one whose choice depends on another option's current selection — e.g.
   Face depending on Skin Color, or Markings Color/Tattoo Color/Hair Style
   on their own drivers) is now named
   `f"{option_name} ({driving_choice_name})"` — one submenu per (driving
   choice, dependent option) pair, never the bare option name — so the
   panel silently treats these as absent (its own documented behavior for
   "node graph was never built", not a crash). Plain independent options
   (Skin Color, Markings, Tattoo, Hair Color, Eye Color, Scars, Eyesight)
   are unaffected. Fixing this needs the panel to understand "one row per
   (dependent option, driving choice) pair, only the currently-relevant
   one meaningful for the presently-selected driving choice" — a real new
   UI shape, not attempted yet; needs a design call on how to present that
   before implementing.
