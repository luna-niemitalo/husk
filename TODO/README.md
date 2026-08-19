# Index of TODO files

Navigation aid, not a punch list itself — each file below still follows its
own "open punch list, not a historical record" convention (fixed items get
removed outright, git history is the record). This index is a live
snapshot of what's in `TODO/`, not a log; update it in place as files
close/open/split, don't append entries here.

**Gate** column: whether the file's own next step needs Luna's own
interactive/real-client judgment (billboard-style ground-truthing, a
design decision only she can make) or is independently solvable (data
work, corpus scans, well-scoped implementation, investigation).

| File | Scope | Status | Gate |
|---|---|---|---|
| `MULTI_TEXTURE_LAYER_TODO.md` | Multi-texture-layer (`textureCount > 1`) combiner rendering — **the single biggest visual-fidelity lever left** (~79% of the real `.skin` corpus) | Steps 0-5 all done (`shader_id` parsing/resolution, env-map UV wiring, Blender-side combiner-formula rendering, real corpus verification 2026-08-17); one open follow-up remains, quantifying the image-dedup-collision fix's real corpus frequency | Independent |
| `PIXEL_SHADER_FORMULAS_TODO.md` | Filling wowdev.wiki's 17 undocumented `Combiners_*` formulas | A promising but unverified lead found (`reference/wow.export`'s shader); step 1 (find real corpus repros) done 2026-08-14 — 14/17 have real repros, some with thousands of files | Step 2 (verify against real rendered output): human-gated |
| `RENDER_QUALITY_TODO.md` | Corpus-review render-quality findings (rotation, textures, alpha, billboards) | Rotation shear + V-scroll direction + `alphaCutoff` fixed (2026-08-14); 68 "unexplained" blank renders downgraded 2026-08-14 (6 spot-checked, 0 real bugs found); Mod/Mod2x multiply-blend compositing implemented and verified 2026-08-15 (`MOD_BLEND_COMPOSITING_TODO.md` closed and deleted); billboard ground-truth, ambiguous-pool tiebreak still open | Billboard ground-truth is human-gated; ambiguous-pool tiebreak independent |
| `CHAR_TEXTURE_COMPOSITING_TODO.md` | Full DB2-driven character texture compositing (base/overlay skin layers) | Stages 1-2 done (WDC5 parsing, real placement geometry via `--char-layout-id`); Stage 3 (choice chain) blocked on `ChrCustomizationOption`/`_Choice`/`_Category` — confirmed 2026-08-19 these were never downloaded to the local WoW install at all, not just un-extracted, so no `casc-tool` re-extraction can fix it; routes through `tact-fetch`'s CDN-fetch step instead, not yet implemented there; Stages 4-5 (pixel compositing, Blender picker) not started | Independent, but Stage 3 is genuinely blocked on `tact-fetch`, not something to work around |
| `KNOWLEDGE_BASE_DESIGN.md` | A husk-owned DB2/listfile knowledge-base design, plus the object-skin-texture-resolution fix it grew out of | Object-skin texture resolution fixed 2026-08-16 — not via the DB2/knowledge-base chain (too collision-prone, kept disabled) but via a new local race/gender-suffix fallback tier in the existing fuzzy-basename matcher; only remaining step is running the real full-corpus render to completion and a visual spot-check | Independent |
| `BONE_CORRECTION_APPLICATION_TODO.md` | Applying `.bone` correction matrices in Blender, now that selection is resolved | Selection done (2026-08-14); application semantics (multiply order, space) never verified against real client behavior | **Human-gated** — needs a real side-by-side comparison, same as billboard alignment did |
| `ENGINE_TODO.md` | External-data gaps, and which are actually husk's to close (renumbered 2026-08-14 after former items 1/2 resolved+removed) | #1 hardcoded texture resolution (biggest remaining item, tracked in `CHAR_TEXTURE_COMPOSITING_TODO.md`); #2 `aliasNext` names checked and closed 2026-08-14 (local DB2 schema dropped `Name` around 7.3.5, unrecoverable); #3 `blendTimeOperation` (no data, needs a heuristic); #4 sound linking (unconfirmed); #5 LOD thresholds (a design decision, not a gap) | #4: independent investigation. #3: independent (author a heuristic). #5: a decision, not a task |
| `BONE_NAME_DEDUCTION_TODO.md` | Tier-2 bone naming (reference-skeleton matching) | Not started; cosmetic only, zero visual/render impact | Independent |
| `CLEANUP_TODO.md` | Code hygiene: comment-hygiene sweep, a dangling-internal-reference corpus scan | `export_materials.cpp` split done 2026-08-14 (`export_texture_resolution.hpp/.cpp` carved out); comment-hygiene sweep two passes done, real remaining scope still open; dangling-reference scan not started | Independent |
| `DPIV_TODO.md` | Cracking the `DPIV` mystery chunk's real field semantics | Structural shape characterized (multi-record, ground-anchor-shaped); `field3`'s real meaning still open | Independent (corpus/byte analysis), but world/doodad-placement-adjacent, not core M2 rendering |
| `TODO_correctness.md` | Misc correctness/usability gaps punch list, including the `tools/live_gallery` browser-viewer verification follow-up | Camera support deprioritized by design; `.bone` slot *selection* now resolved (2026-08-14) — application is `BONE_CORRECTION_APPLICATION_TODO.md`'s job, not this file's; live-viewer curve playback structurally verified, not yet visually confirmed in a real browser; `resolveFieldString`'s multi-section string-offset bug fixed 2026-08-20 | N/A — mostly closed or delegated elsewhere |
| `WORLD/` | WMO/ADT/world-geometry scope (12 files) — a separate project phase, own entry point `../WORLD_COMPLETENESS.md` | Investigated/planned, no code started | Not covered by this index — see `WORLD_COMPLETENESS.md` |

## Suggested order, independent tasks only

1. `KNOWLEDGE_BASE_DESIGN.md` — run the pending full-corpus render to
   completion and do a real visual spot-check (object-skin resolution
   itself is already fixed).
2. `CLEANUP_TODO.md` — pure hygiene, no functional payoff, do when nothing
   higher-value is available.

Everything else either needs Luna's own interactive/client-side
verification (`BONE_CORRECTION_APPLICATION_TODO.md`, `RENDER_QUALITY_TODO.md`'s
billboard ground-truth pass, `PIXEL_SHADER_FORMULAS_TODO.md` step 2) or is
genuinely blocked on `tact-fetch`'s not-yet-implemented CDN-fetch step
(`CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 3 — confirmed 2026-08-19 this is
data never downloaded locally, not a `casc-tool` re-extraction case).
