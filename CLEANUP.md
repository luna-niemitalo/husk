# CLEANUP.md

Open punch list from a style/compliance audit against `~/nix/claude-rules`.
Fixed items get removed outright — git history is the record, not this
file. See also `TODO/CLEANUP_TODO.md` for the project's existing (unrelated)
cleanup punch list.

1. **A much larger `TODO: Remove: FAILURES(2).md #N` comment cluster exists
   in older `src/`/`tests/` files, predating this pass's scope.** The
   original 2026-09-04 audit and cleanup pass only covered files changed
   since the last comment-hygiene sweep (`46844f6d`) and fixed 19 sites
   there. A broader grep turned up ~25 more sites in files outside that
   scope — real production code, not test-only: `src/m2_primitives.cpp:253,282`,
   `src/m2_primitives.hpp:131`, `src/gltf_mesh.cpp:296`, `src/gltf_mesh.hpp:348`,
   `src/dump_chunks_misc.hpp:96`, `src/m2_animation.cpp:334`,
   `src/m2_animation.hpp:106,175,288`, `src/m2_skeleton.cpp:22,71`,
   `src/skin.cpp:95`, `src/m2_header.cpp:453`, `src/cmd_info.cpp:114`,
   plus several more `tests/*.cpp`/`.hpp` sites
   (`test_cli_errors.cpp:78`, `test_skel.cpp:270`,
   `test_cli_fixtures_scenes.hpp:723,749`, `test_m2_primitives.cpp:226,407`,
   `test_cli.cpp:10-11,423`, `test_cli_fixtures.hpp:181,355,378`) and one
   in `tools/corpus_scan_tasks/casc_size_mismatch_task.py:100`. `FAILURES.md`
   and `FAILURES2.md` themselves no longer exist in the repo — every one of
   these is a dangling reference to an already-deleted doc, already marked
   "Remove" by its own author. Not fixed this pass (out of scope, flagged
   rather than silently expanded into); same mechanical trim as the 19
   sites already done — delete the `TODO: Remove: FAILURES(2).md ...`
   citation, keep any independently-standing why-comment around it.

2. **`TODO/KNOWLEDGE_BASE_DESIGN.md` / `husk db2-build` / `knowledge.sqlite`
   — open design decision, not yet actionable.** A SQLite store ingesting 7
   DB2 tables was built to consolidate scattered DB2 lookups, but the
   triggering problem (object-skin texture resolution) was fixed by a
   20-line local fallback tier before the database was finished. It sits
   disabled today (no real export path passes `--knowledge-db`) and is
   documented as producing known-wrong same-slot collisions. Not a clean
   delete-candidate: the disambiguator it was missing (`ItemAppearance`
   existence-checking a candidate `ItemDisplayInfoID`) now exists in the
   tree via the unrelated `itemappearance_db2.cpp` (built later for
   equipped-gear resolution). See `TODO/KNOWLEDGE_BASE_DESIGN.md`'s
   "Proposed robustness follow-up" section for the cross-validation design
   that could make this join trustworthy instead of discarding it. Until
   that's built (or the idea is rejected), the subsystem stays exactly as
   disabled/non-default as it is now — don't wire `--knowledge-db` into any
   real export path on the strength of the idea alone.
