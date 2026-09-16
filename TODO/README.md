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

**Scope** column: legacy-only (wired to the legacy `gltf_*`/`cmd_export.cpp`
pipeline, due for a redo under `canon::` regardless), dual-use/infra
(shared code or a semantic question `canon::` will hit too), or
investigative (corpus/data analysis, independent of which pipeline
ships) — each file also carries this tag near its own top.

| File | Scope | Status | Gate |
|---|---|---|---|
| `TEXTURE_POOL_RECALL_TODO.md` | dual-use/infra | The fuzzy candidate pool's `startswith(model_basename)` gate — **the single biggest correctness lever left** — plus two adjacent object-skin/DB2-disambiguation threads merged in from the now-deleted `TODO_correctness.md`/`KNOWLEDGE_BASE_DESIGN.md`. Recall on `bloodelffemale_hd` went 15.9% -> 91.3%, but a supervisor re-check found the right skin file is now reachable and still isn't picked — ranking, not recall, is now the binding constraint. Open items: (1) re-tune `orderCandidatesForDefault`'s ranking for hundred-candidate pools, (2) decide whether claim-and-remove is worth keeping, (3) a distinct wrong-texture-on-runtime-slot bug (`weapon_blade`), (4) the human-gated full-corpus render/spot-check, (5) an unstarted `itemappearance_db2` cross-validation proposal | Independent, except (4) which is human-gated; final "does this character look right" call is Luna's |
| `PIXEL_SHADER_FORMULAS_TODO.md` | investigative | Filling wowdev.wiki's 17 undocumented `Combiners_*` formulas. A full corpus-wide invariant/equivalence-testing pass verified 3 formulas exact, found 3 more real-but-ambiguous, 4 genuine negatives, plus a weak-signal lead set for `Illum` | Step 2 (final verification against real rendered output): human-gated |
| `RENDER_QUALITY_TODO.md` | legacy-only | Corpus-review render-quality findings (rotation, textures, alpha, billboards). Rotation shear, V-scroll direction, `alphaCutoff`, and Mod/Mod2x compositing all fixed and verified; billboard ground-truth and several unconfirmed render-artifact repros (black silhouette, disco/flicker, white silhouette) still open, see `INVESTIGATIONS_TODO.md` items 5-10 | Billboard ground-truth is human-gated; the rest are independent investigations |
| `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` | legacy-only | Blender-side live customization-choice texture switch. Implemented and structurally verified against real DB2-resolved data. Two open items: a real interactive GUI pass with real per-choice texture bytes; a `husk_blender_options_panel.py` socket-name gap for dependent options | Independent to keep implementing; final visual pass is human-gated |
| `EQUIPPED_GEAR_RENDER_TODO.md` | legacy-only | Blender-side rendering of the DB2-resolved equipped-gear appearance. Case 1 (standalone-geometry attachment) is done and verified. Open: case 2 (object-skin texture-overlay compositing, not started), a real interactive GUI pass, and a weapon sheath-state-toggle stretch goal | Independent to scope/implement; final visual pass is human-gated |
| `BONE_CORRECTION_APPLICATION_TODO.md` | dual-use/infra | Applying `.bone` correction matrices in Blender, now that selection is resolved. Application semantics (multiply order, space) never verified against real client behavior | **Human-gated** — needs a real side-by-side comparison, same as billboard alignment did |
| `ENGINE_TODO.md` | dual-use/infra | External-data gaps. #1 (`aliasNext` names) and #4 (LOD thresholds) are both settled dead-ends (condensed, full investigation in `CLAUDE_HISTORY.md`); #2 (`blendTimeOperation`, no data, needs a heuristic) and #3 (sound linking, split real/blocked) remain open | #2/#3: independent investigation/heuristic-authoring. #1/#4: closed, not actionable |
| `BONE_NAME_DEDUCTION_TODO.md` | investigative | Tier-2 bone naming (reference-skeleton matching). Not started; cosmetic only, zero visual/render impact | Independent |
| `CLEANUP_TODO.md` | investigative | One open item: `m2_full_validation_task.py` genuinely hangs (zero progress, not just slow) on a real full-corpus run despite clean bounded reproductions — root cause not yet found, needs a live `strace`/`py-spy` attach, not more guessing | Independent investigation, needs a live hang repro |
| `DPIV_TODO.md` | investigative | Cracking the `DPIV` mystery chunk's real field semantics. Structural shape and fields 0/1 characterized (bbox-center coordinates); field2 (ground-contact anchor) and field3 (small integer tag) still open, corpus-statistics levers exhausted — next step is a human visual grid render | Human-gated (the visual grid read is Luna's own pattern-spotting); assembling the renders is independent |
| `WORLD/` | — | WMO/ADT/world-geometry scope (12 files) — a separate project phase, own entry point `../WORLD_COMPLETENESS.md` | Not covered by this index — see `WORLD_COMPLETENESS.md` |
| `INVESTIGATIONS_TODO.md` | investigative | Meta-list of every open investigation-shaped item scattered across the files above — 13 catalogued, 7 done as of 2026-09-16, 6 still open (mostly `RENDER_QUALITY_TODO.md`-adjacent render-artifact repros) | Independent (that's the whole point of the list); a human-gated section is kept for completeness only |
| `RENDER_PIPELINE_DRIFT_TODO.md` | legacy-only | `render_glb.py`'s previews never run `husk_blender_geoset_mask.py`'s customization/geoset-switch logic, so they don't represent the real two-step Blender pipeline. Still undecided: opt-in switch step vs. documenting the raw-geometry-only limitation. Flagged for absorption into `../REFACTOR/BLENDER_ADDON.md` once the canon:: addon replaces both tools | Independent to scope/decide; the drift itself is a design decision |
| `../REFACTOR/` | — | Target four-stage pipeline (`parse -> resolve -> canonical -> write`), the single resolution boundary, the native bundle format + Blender addon, and the full duplicated/divergent-path audit. Stage 3 deep in progress; `../CANONICAL_FORMAT.md` (repo root) is now the canonical `canon::`/bundle explainer for a reader outside this project | Own index and gate table at `../REFACTOR/README.md`; supersedes nothing in this table except `RENDER_PIPELINE_DRIFT_TODO.md` above |

## Suggested order, independent tasks only

Real DB2-driven character texture compositing, the live Blender-side
customization-choice switch, and equipped-gear appearance resolution
(case 1) are all done and verified end to end (the Blender switch's own
real-texture interactive pass is human-gated, see
`CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`). `DPIV_TODO.md`'s corpus-statistics
work is exhausted; its remaining step is a human visual read.
`TEXTURE_POOL_RECALL_TODO.md`'s ranking retune is the single biggest
independent lever left.

Everything else needs Luna's own interactive/client-side verification
(`BONE_CORRECTION_APPLICATION_TODO.md`, `RENDER_QUALITY_TODO.md`'s
billboard ground-truth pass, `PIXEL_SHADER_FORMULAS_TODO.md` step 2,
`TEXTURE_POOL_RECALL_TODO.md`'s full-corpus render item — this one bit a
previous session twice, see its own gate note there).
