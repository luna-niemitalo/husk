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
| `MULTI_TEXTURE_LAYER_TODO.md` | Multi-texture-layer (`textureCount > 1`) combiner rendering — **the single biggest visual-fidelity lever left** (~79% of the real `.skin` corpus) | `shader_id` parsing/resolution done; Blender-side formula rendering and env-map-frequency measurement (step 0) still open | Independent (step 0 is a corpus scan; step 4/5 Blender rendering is implementation, not client comparison) |
| `PIXEL_SHADER_FORMULAS_TODO.md` | Filling wowdev.wiki's 17 undocumented `Combiners_*` formulas | A promising but unverified lead found (`reference/wow.export`'s shader); step 1 (find real corpus repros) is open and easy | Step 1: independent. Step 2 (verify against real rendered output): human-gated |
| `RENDER_QUALITY_TODO.md` | Corpus-review render-quality findings (rotation, textures, alpha, billboards) | Rotation shear + V-scroll direction fixed (2026-08-14); 68 unexplained blank renders, Mod/Mod2x blend modes + `alphaCutoff`, billboard ground-truth, ambiguous-pool tiebreak all open | Mixed — blank-render investigation and Mod/Mod2x are independent; billboard ground-truth is human-gated |
| `CHAR_TEXTURE_COMPOSITING_TODO.md` | Full DB2-driven character texture compositing (base/overlay skin layers) | Stages 1-2 done (WDC5 parsing, real placement geometry via `--char-layout-id`); Stage 3 (choice chain) blocked on 0-byte local `ChrCustomizationOption`/`_Choice`; Stages 4-5 (pixel compositing, Blender picker) not started | Independent, but Stage 3 is genuinely blocked on a `casc-tool` re-extraction, not something to work around |
| `BONE_CORRECTION_APPLICATION_TODO.md` | Applying `.bone` correction matrices in Blender, now that selection is resolved | Selection done (2026-08-14); application semantics (multiply order, space) never verified against real client behavior | **Human-gated** — needs a real side-by-side comparison, same as billboard alignment did |
| `ENGINE_TODO.md` | External-data gaps, and which are actually husk's to close (renumbered 2026-08-14 after former items 1/2 resolved+removed) | #1 hardcoded texture resolution (biggest remaining item, tracked in `CHAR_TEXTURE_COMPOSITING_TODO.md`); #2 `aliasNext` names (small, unattempted); #3 `blendTimeOperation` (no data, needs a heuristic); #4 sound linking (unconfirmed); #5 LOD thresholds (a design decision, not a gap) | #2/#4: independent investigation. #3: independent (author a heuristic). #5: a decision, not a task |
| `BONE_NAME_DEDUCTION_TODO.md` | Tier-2 bone naming (reference-skeleton matching) | Not started; cosmetic only, zero visual/render impact | Independent |
| `CLEANUP_TODO.md` | Code hygiene: splitting `export_materials.cpp`, comment-hygiene sweep, a dangling-internal-reference corpus scan | Comment-hygiene sweep partial; the other two not started, and the file split needs a real call-graph read before committing to seams | Independent |
| `DPIV_TODO.md` | Cracking the `DPIV` mystery chunk's real field semantics | Structural shape characterized (multi-record, ground-anchor-shaped); `field3`'s real meaning still open | Independent (corpus/byte analysis), but world/doodad-placement-adjacent, not core M2 rendering |
| `TODO_correctness.md` | Misc correctness/usability gaps punch list | Camera support deprioritized by design; `.bone` slot *selection* now resolved (2026-08-14) — application is `BONE_CORRECTION_APPLICATION_TODO.md`'s job, not this file's | N/A — mostly closed or delegated elsewhere |
| `WORLD/` | WMO/ADT/world-geometry scope (12 files) — a separate project phase, own entry point `../WORLD_COMPLETENESS.md` | Investigated/planned, no code started | Not covered by this index — see `WORLD_COMPLETENESS.md` |

## Suggested order, independent tasks only

Roughly matching the priority discussion in `CLAUDE_HISTORY.md`'s most
recent entries — highest visual-fidelity payoff per unit of (unsupervised)
effort first:

1. `PIXEL_SHADER_FORMULAS_TODO.md` step 1 / `MULTI_TEXTURE_LAYER_TODO.md`
   step 0 — both are small, well-defined corpus scans that unblock a
   priority call or a human-verification step later, without being one
   themselves.
2. `ENGINE_TODO.md` #2 (`aliasNext` names) — cheap, mechanical, check-then-
   implement.
3. `RENDER_QUALITY_TODO.md`'s 68 unexplained blank renders — real bugs,
   concrete repro list already in hand, likely several independent root
   causes.
4. `RENDER_QUALITY_TODO.md`'s Mod/Mod2x blend modes + explicit
   `alphaCutoff` — same shape as the already-shipped additive-blend fix.
5. `CLEANUP_TODO.md` — pure hygiene, no functional payoff, do when nothing
   higher-value is available.

Everything else either needs Luna's own interactive/client-side
verification (`BONE_CORRECTION_APPLICATION_TODO.md`, `RENDER_QUALITY_TODO.md`'s
billboard ground-truth pass, `PIXEL_SHADER_FORMULAS_TODO.md` step 2) or is
genuinely blocked on a `casc-tool` re-extraction
(`CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 3).
