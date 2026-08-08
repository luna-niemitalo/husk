# texture_type_collisions: what happened and what it found

## Why the original single-threaded run was interrupted

`tools/find_texture_type_collisions.py` was started single-threaded against
the full ~130k-file corpus and had been running ~55 minutes with zero
output (it only writes its CSV/log at the very end, so nothing was lost
by stopping it early). A same-session benchmark on an identical 2000-file
sample showed a real parallel implementation of the same check
(`tools/corpus_scan_framework.py` + `tools/corpus_scan_tasks/
texture_type_collisions_task.py`, a thin wrapper reusing the original
script's own `analyze()`/`summarize()` logic unchanged) ran ~9x faster
(23 files/sec sequential vs 213 files/sec parallel) — with the old run's
own multi-hour projected total against a few minutes. With Luna's
explicit approval, the old process was killed (`SIGTERM`, confirmed
terminated) and immediately replaced by the parallel run.

**Result**: the parallel run finished the entire corpus in 17m14s
(130,576 files, 126 files/sec average) instead of the several hours the
original was headed for.

## Where the results are

- `texture_type_collisions.csv` (repo root) — one row per `.m2` file with
  >=1 real `M2Texture.type` collision (66,847 rows), full per-file detail
  (colliding types, batch-resolution counts, agreement/divergence with
  `textureLookup`).
- `texture_type_collisions_scan.log` (repo root) — the same aggregate
  summary printed at the end of the run.
- `tools/corpus_scan_framework.py` / `tools/corpus_scan_tasks/
  texture_type_collisions_task.py` — the parallel driver and task
  definition used to produce the above, if this needs to be re-run.
- `tools/find_texture_type_collisions.py` — the original script, left
  untouched and still runnable standalone; the parallel task module
  imports its `analyze()`/`summarize()` directly rather than duplicating
  the logic.

## Findings beyond the original headline number

The original question ("does `textureLookup` ever disagree with the
per-batch `textureCombos` resolution") was already answered (69.1% of
collision files have >=1 disagreeing batch). Digging into the full-corpus
CSV turned up several things not visible from a 2000-file sample:

1. **Every single collision is on `M2Texture.type == 0`.** Across all
   66,847 affected files, `colliding_types` never contains anything but
   `0:<n>`. Types 1-8 (skin/hair/etc. — the player-customization slots)
   never collide anywhere in the corpus. A consumer only needs to worry
   about `textureLookup` ambiguity for hardcoded (type 0) textures.

2. **69.1% of collision files have a real diverging batch, but 25.6%
   (17,094 files) don't touch the ambiguity at all** — the colliding
   textures exist in the M2Texture array but no `.skin` batch's
   `textureCombos` chain ever resolves to them. For those files the
   collision is inert: present in the data, invisible in practice. Only
   ~74% of collision files (49,753) have the ambiguity actually exercised
   by at least one batch.

3. **This is overwhelmingly a world/item/spell problem, not a character
   problem.** By top-level corpus directory: `world` 30,368, `item`
   22,931, `spells` 8,753, `creature` 2,924 — but `character` only **89**
   files (0.13% of all collisions). If husk's priority is character
   rigging/animation fidelity, this specific ambiguity barely touches
   that surface.

4. **The severity tail is extreme but explainable.** Median
   `colliding_texture_count` is 3, but the top files reach 76-137 (e.g.
   `interface/glues/models/ui_mainmenu_dragonflight/
   ui_mainmenu_dragonisles.m2` at 137). All of the worst offenders are UI
   glue-screen models, spell-effect models, or world doodads — models
   that legitimately bake in dozens of distinct, non-interchangeable
   hardcoded textures, not evidence of a data-corruption pattern.

5. **Two small structural edge cases, both negligible in size**: 112
   collision files (0.17%) have no resolvable same-basename `.skin`
   sibling at all, and `has_texture_lookup` is `True` for literally every
   affected file (no case where a collision exists but the reverse-lookup
   table itself is absent/empty). Worth knowing if `CORPUS_TODO.md`'s
   still-open "~7 unverified `textureComboIndex`-out-of-range cases"
   thread is picked up again — this 112-file figure is a related,
   now-precisely-counted data point, not the same check, but adjacent.
