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
| `MULTI_TEXTURE_LAYER_TODO.md` | Multi-texture-layer (`textureCount > 1`) combiner rendering — **the single biggest visual-fidelity lever left** (~79% of the real `.skin` corpus) | `shader_id` parsing/resolution done; step 0 (env-map-frequency measurement) done 2026-08-14 — 41.33% of real batches are `_Env`-bearing, confirmed high-priority; Blender-side formula rendering (step 4/5) still open | Independent (step 4/5 Blender rendering is implementation, not client comparison) |
| `PIXEL_SHADER_FORMULAS_TODO.md` | Filling wowdev.wiki's 17 undocumented `Combiners_*` formulas | A promising but unverified lead found (`reference/wow.export`'s shader); step 1 (find real corpus repros) done 2026-08-14 — 14/17 have real repros, some with thousands of files | Step 2 (verify against real rendered output): human-gated |
| `RENDER_QUALITY_TODO.md` | Corpus-review render-quality findings (rotation, textures, alpha, billboards) | Rotation shear + V-scroll direction + `alphaCutoff` fixed (2026-08-14); 68 "unexplained" blank renders downgraded 2026-08-14 (6 spot-checked, 0 real bugs found); Mod/Mod2x's design question resolved (Cryptomatte + Compositor, verified interactively) — implementation tracked separately, see `MOD_BLEND_COMPOSITING_TODO.md`; billboard ground-truth, ambiguous-pool tiebreak still open | Mod/Mod2x is independent implementation work now, not a design call (see `MOD_BLEND_COMPOSITING_TODO.md`); billboard ground-truth is human-gated |
| `MOD_BLEND_COMPOSITING_TODO.md` | Implementing real Mod/Mod2x (multiply) blend compositing in the post-import Blender script | Technique confirmed (Cryptomatte + Compositor `Add`/`Multiply` math, no material-shader-graph trick exists); the EEVEE Cryptomatte bug found this session is root-caused and fixed (`surface_render_method` must be re-asserted to `DITHERED` after setting `blend_method`) — node graph itself not yet built | Independent, not a design call |
| `CHAR_TEXTURE_COMPOSITING_TODO.md` | Full DB2-driven character texture compositing (base/overlay skin layers) | Stages 1-2 done (WDC5 parsing, real placement geometry via `--char-layout-id`); Stage 3 (choice chain) blocked on 0-byte local `ChrCustomizationOption`/`_Choice`; Stages 4-5 (pixel compositing, Blender picker) not started | Independent, but Stage 3 is genuinely blocked on a `casc-tool` re-extraction, not something to work around |
| `BONE_CORRECTION_APPLICATION_TODO.md` | Applying `.bone` correction matrices in Blender, now that selection is resolved | Selection done (2026-08-14); application semantics (multiply order, space) never verified against real client behavior | **Human-gated** — needs a real side-by-side comparison, same as billboard alignment did |
| `ENGINE_TODO.md` | External-data gaps, and which are actually husk's to close (renumbered 2026-08-14 after former items 1/2 resolved+removed) | #1 hardcoded texture resolution (biggest remaining item, tracked in `CHAR_TEXTURE_COMPOSITING_TODO.md`); #2 `aliasNext` names checked and closed 2026-08-14 (local DB2 schema dropped `Name` around 7.3.5, unrecoverable); #3 `blendTimeOperation` (no data, needs a heuristic); #4 sound linking (unconfirmed); #5 LOD thresholds (a design decision, not a gap) | #4: independent investigation. #3: independent (author a heuristic). #5: a decision, not a task |
| `BONE_NAME_DEDUCTION_TODO.md` | Tier-2 bone naming (reference-skeleton matching) | Not started; cosmetic only, zero visual/render impact | Independent |
| `CLEANUP_TODO.md` | Code hygiene: comment-hygiene sweep, a dangling-internal-reference corpus scan | `export_materials.cpp` split done 2026-08-14 (`export_texture_resolution.hpp/.cpp` carved out); comment-hygiene sweep two passes done, real remaining scope still open; dangling-reference scan not started | Independent |
| `DPIV_TODO.md` | Cracking the `DPIV` mystery chunk's real field semantics | Structural shape characterized (multi-record, ground-anchor-shaped); `field3`'s real meaning still open | Independent (corpus/byte analysis), but world/doodad-placement-adjacent, not core M2 rendering |
| `TODO_correctness.md` | Misc correctness/usability gaps punch list | Camera support deprioritized by design; `.bone` slot *selection* now resolved (2026-08-14) — application is `BONE_CORRECTION_APPLICATION_TODO.md`'s job, not this file's | N/A — mostly closed or delegated elsewhere |
| `WORLD/` | WMO/ADT/world-geometry scope (12 files) — a separate project phase, own entry point `../WORLD_COMPLETENESS.md` | Investigated/planned, no code started | Not covered by this index — see `WORLD_COMPLETENESS.md` |

## Suggested order, independent tasks only

Roughly matching the priority discussion in `CLAUDE_HISTORY.md`'s most
recent entries — highest visual-fidelity payoff per unit of (unsupervised)
effort first:

1. ~~`PIXEL_SHADER_FORMULAS_TODO.md` step 1 / `MULTI_TEXTURE_LAYER_TODO.md`
   step 0~~ — **both done 2026-08-14**, one scan
   (`tools/corpus_scan_tasks/shader_names_task.py`) answered both: env-map
   frequency is 41.33% of real batches (was assumed rare), and 14/17
   undocumented pixel shaders have real corpus repros. Next real payoff in
   each file is now implementation (`MULTI_TEXTURE_LAYER_TODO.md` step 4/5)
   or human-gated verification (`PIXEL_SHADER_FORMULAS_TODO.md` step 2).
2. `MOD_BLEND_COMPOSITING_TODO.md` — Mod/Mod2x blend modes. **Not** a
   material-shader-graph node-recipe port like additive was (no
   framebuffer-read primitive in a material shader graph, on either
   engine), but Cryptomatte + Compositor `Add`/`Multiply` math on a single
   render *is* the right technique, confirmed interactively. A real EEVEE
   Next Cryptomatte bug found and root-caused this session (silently
   triggered by `blend_method='BLEND'` flipping `surface_render_method` to
   `BLENDED`) has a real fix (re-assert `DITHERED`) — remaining work is
   just the node graph itself.
3. `CLEANUP_TODO.md` — pure hygiene, no functional payoff, do when nothing
   higher-value is available.

(`RENDER_QUALITY_TODO.md`'s 68 "unexplained" blank renders and its
`alphaCutoff` item were both here — checked 2026-08-14: `alphaCutoff` is
fixed, and the blank-render bucket turned out to be mostly correctly-
blank-by-design once spot-checked, not a bug pile; see that file's §2 for
the real remaining follow-up.)

(`ENGINE_TODO.md` #2, `aliasNext` names, was here — checked 2026-08-14 and
closed as genuinely unfulfillable, see that file.)

Everything else either needs Luna's own interactive/client-side
verification (`BONE_CORRECTION_APPLICATION_TODO.md`, `RENDER_QUALITY_TODO.md`'s
billboard ground-truth pass, `PIXEL_SHADER_FORMULAS_TODO.md` step 2) or is
genuinely blocked on a `casc-tool` re-extraction
(`CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 3).
