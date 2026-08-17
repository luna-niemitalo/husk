# TODO: exploring and wiring the missing texture/data links

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was
fixed and when, not this file.

## Open

1. **Full corpus re-render.** `src/export_texture_resolution.cpp`'s
   `scanFuzzyTexturePool` now strips a real, corpus-verified race/gender
   suffix (`stripRaceGenderSuffix`, `TODO/KNOWLEDGE_BASE_DESIGN.md`'s
   "local fallback" section) when the exact-basename scan finds nothing —
   verified live against both real cases that exposed the DB2 chain's
   correctness bug: `helm_leather_pvpdruid_b_02_scm.m2` (sole real
   candidate, embedded directly) and `chest_mail_chainmailset_b_01_go_f.m2`
   (5 real recolor variants, all embedded as `alternate_textures` extras,
   one picked as default, honestly labeled non-deterministic). No DB2/
   `--knowledge-db` involvement — pure local directory + verified naming
   convention, always on, no flag needed. `render_sample_driver.py`/
   `tools/full_render.py` need a real run to completion:
   `direnv exec . tools/venv/bin/python tools/full_render.py`, then a
   real visual check of the output. Old renders already cleared to
   `trash/` for a clean run.
