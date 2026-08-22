# TODO: real character texture compositing (DB2-driven) — closed

**Status: closed, all 6 stages done.** Kept as a stub, not deleted, only
because it's cited by name from `DESIGN.md`'s Non-goals, `WIKI_FINDINGS*`,
`TOOL_COMPARISON.md`, `EYES_ON_FINDINGS.md`, and several `src/*.hpp` doc
comments — full narrative is git history / `CLAUDE_HISTORY.md`, not this
file. Nothing open here; for remaining work in this problem space see
`CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` (live customization-texture switch)
and `EQUIPPED_GEAR_RENDER_TODO.md` (equipped-gear rendering).

## Scope clarification (cited from `DESIGN.md`'s Non-goals)

Luna, 2026-08-08: "the only hard boundary is not loading casc tool as a
dependency... all data in wow_export is free for all, to be used." A raw
`.db2` file already extracted locally by `casc-tool` is in scope for husk
to parse — the Non-goals boundary is about *live* CASC/DB2 access, not
locally-extracted files already on disk.

## What was built (stages 1-6)

1. WDC5 parser + WoWDBDefs column-name resolution (`src/db2.hpp`/`.cpp`,
   `src/dbd.hpp`/`.cpp`).
2. Real placement geometry (base atlas size, section rects, texture-layer
   blend info) attached as `chr_texture_layout` skin extras
   (`--db2-dir`/`--dbd-dir`/`--char-layout-id`).
3. The `ChrCustomizationOption → Choice → Element → Material →
   TextureFileData` chain, resolving real choice IDs to real texture
   FileDataIDs (`chr_enabled_materials`), plus the full customization menu
   attached automatically (`chr_customization_options`).
4. Real pixel compositing was built, verified, then deliberately
   reverted — compositing belongs in Blender, not husk. See
   `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` for why and what replaced it.
5. Blender-side live customization-choice texture switch — implemented,
   fully tracked in `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`.
6. Equipped-gear appearance resolution
   (`ItemModifiedAppearanceID → ... → FileDataID`) via `husk
   appearance-string --db2-dir/--dbd-dir` — done; rendering/attaching the
   resolved gear is separate, tracked in `EQUIPPED_GEAR_RENDER_TODO.md`.
