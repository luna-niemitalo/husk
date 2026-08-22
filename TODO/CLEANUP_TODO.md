# TODO: cleanup & follow-ups

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

1. **Retarget/remove citations of the now-stub `CHAR_TEXTURE_COMPOSITING_
   TODO.md`, then delete the stub file itself.** It was pruned to a
   closed-stub summary (2026-08-22) but kept at its path solely because
   ~20 other files cite it by name; those citations should point at
   whichever file actually owns the content now
   (`CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` for the live customization-
   texture switch, `EQUIPPED_GEAR_RENDER_TODO.md` for gear rendering, or
   plain git history for anything else) instead of a stub whose only job
   is being cited. Once nothing live points at it, delete it outright —
   same "closed items get removed" convention every other TODO file here
   follows.

   Live docs/doc-comments to check and retarget (not exhaustive — re-grep
   before starting): `DESIGN.md` (Non-goals section + several inline
   citations), `README.md`, `TOOL_COMPARISON.md`, `EYES_ON_FINDINGS.md`,
   `TODO/ENGINE_TODO.md`, `TODO/EQUIPPED_GEAR_RENDER_TODO.md`,
   `TODO/RENDER_QUALITY_TODO.md`, `TODO/TODO_correctness.md`,
   `TODO/README.md`'s own index row, `tools/husk_blender_geoset_mask.py`,
   and doc comments in `src/chrcustomization_db2.hpp`,
   `src/chrmodel_db2.hpp`, `src/cmd_appearance.cpp`, `src/cmd_db2.cpp`,
   `src/commands.hpp`, `src/db2.hpp`, `src/db2table.hpp`,
   `src/gltf_skeleton.hpp`, `src/itemappearance_db2.hpp`,
   `src/modelfiledata_db2.hpp`, `src/texturefiledata_db2.hpp`, and the
   `tests/test_cli_*.cpp` files that mention it in comments.

   **Exempt, leave as-is**: `CLAUDE_HISTORY.md` and
   `WIKI_FINDINGS_HISTORY.md` — both are dated logs of what happened at
   the time, same "don't rewrite history" treatment git commits get, not
   live documentation that needs to keep resolving.
