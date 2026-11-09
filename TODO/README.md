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

**Scope** column: legacy-only (written against the legacy glTF pipeline
and its Blender/render tools, all deleted in bdec5ce..1e4b766), dual-use/infra
(shared code or a semantic question `canon::` will hit too), or
investigative (corpus/data analysis, independent of which pipeline
ships) — each file also carries this tag near its own top.

| File | Scope | Status | Gate |
|---|---|---|---|
| `TEXTURE_POOL_RECALL_TODO.md` | dual-use/infra | The fuzzy candidate pool's `startswith(model_basename)` gate — **the single biggest correctness lever left** — plus two adjacent object-skin/DB2-disambiguation threads merged in from the now-deleted `TODO_correctness.md`/`KNOWLEDGE_BASE_DESIGN.md`. Recall on `bloodelffemale_hd` went 15.9% -> 91.3%, but a supervisor re-check found the right skin file is now reachable and still isn't picked — ranking, not recall, is now the binding constraint. Open items: (1) re-tune `orderCandidatesForDefault`'s ranking for hundred-candidate pools, (2) decide whether claim-and-remove is worth keeping, (3) a distinct wrong-texture-on-runtime-slot bug (`weapon_blade`), (4) the human-gated full-corpus render/spot-check, (5) an unstarted `itemappearance_db2` cross-validation proposal | Independent, except (4) which is human-gated; final "does this character look right" call is Luna's |
| `MATERIALS_TODO.md` | dual-use/infra | Material documents (`REFACTOR/MATERIALS.md`): character skin composites built; open: blend mode mapping (mode 15 has no compositor case), layer order, texel sizes, M2/WMO materials as documents | Independent (client-side mapping research, then implementation) |
| `CORPUS_EXPORT_TODO.md` | dual-use/infra | Whole-corpus canonical export (`REFACTOR/CORPUS_EXPORT.md`): the content-addressed store (`store-add`), its FUSE mount, input tracking, `export-corpus` and `index-tree` are built; open: more index tables, unnamed M2s, shared effects/materials/rig, view, prune | Independent; the full run is human-gated |
| `CREATURE_OPTIONS_TODO.md` | dual-use/infra | Creature options (slot texture options, particle colour options) and presets (CreatureDisplayInfo combinations) are exported; open: creatures that alternate between models, humanoid NPC displays, viewer-conditional displays, the fourth texture variation, emitter slots, default texture | Item 1 is a design call (Luna); the rest independent |
| `PIXEL_SHADER_FORMULAS_TODO.md` | investigative | Every combiner formula read from client shader bytecode (`WIKI_FINDINGS/M2/rendering.md`); open: three inferred constant bindings, no client-render comparison | Client-render comparison: human-gated |
| `RENDER_QUALITY_TODO.md` | legacy-only | Render-quality findings from the deleted legacy render pipeline (billboard ground-truth, black/white silhouette, disco/flicker repros; `INVESTIGATIONS_TODO.md` items 5-10). Its tools are gone; deletion candidate | — |
| `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` | legacy-only | The deleted Blender customization texture switch's open items. Subject gone; deletion candidate | — |
| `EQUIPPED_GEAR_RENDER_TODO.md` | legacy-only | Blender-side rendering of gear from the legacy glTF extras. Subject gone (gear is now bundle items, `REFACTOR/BUNDLE_FORMAT.md`); deletion candidate | — |
| `BONE_CORRECTION_APPLICATION_TODO.md` | dual-use/infra | Verifying how `.bone` correction matrices apply, now that the bundle carries them and their selection. Application semantics (multiply order, space) never verified against real client behavior | **Human-gated** — needs a real side-by-side comparison, same as billboard alignment did |
| `ENGINE_TODO.md` | dual-use/infra | External-data gaps. #1 (`aliasNext` names) and #4 (LOD thresholds) are both settled dead-ends (condensed, full investigation in `CLAUDE_HISTORY.md`); #2 (`blendTimeOperation`, no data, needs a heuristic) and #3 (sound linking, split real/blocked) remain open | #2/#3: independent investigation/heuristic-authoring. #1/#4: closed, not actionable |
| `BONE_NAME_DEDUCTION_TODO.md` | investigative | Tier-2 bone naming (reference-skeleton matching). Not started; cosmetic only, zero visual/render impact | Independent |
| `DPIV_TODO.md` | investigative | Cracking the `DPIV` mystery chunk's real field semantics. Structural shape and fields 0/1 characterized (bbox-center coordinates); field2 (ground-contact anchor) and field3 (small integer tag) still open, corpus-statistics levers exhausted — next step is a human visual grid render | Human-gated (the visual grid read is Luna's own pattern-spotting); assembling the renders is independent |
| `WORLD/` | — | WMO/ADT/world-geometry scope (13 files). ADT terrain is implemented (`husk export-terrain`/`export-world`); its open items are in `ADT_TERRAIN_TODO.md`/`WORLD_PLACEMENT_TODO.md`/`LIQUID_TODO.md`. Own entry point `../WORLD_COMPLETENESS.md` | Not covered by this index — see `WORLD_COMPLETENESS.md` |
| `INVESTIGATIONS_TODO.md` | investigative | Meta-list of every open investigation-shaped item scattered across the files above — 14 catalogued (including `CLEANUP_TODO.md`'s former sole item, merged in 2026-09-16), 7 done, 7 still open (mostly `RENDER_QUALITY_TODO.md`-adjacent render-artifact repros, plus the `m2_full_validation_task.py` corpus-scan hang) | Independent (that's the whole point of the list); a human-gated section is kept for completeness only |
| `RENDER_PIPELINE_DRIFT_TODO.md` | legacy-only | Drift between two deleted tools (`render_glb.py`, `husk_blender_geoset_mask.py`); the lesson is in `../REFACTOR/BLENDER_ADDON.md`. Deletion candidate | — |
| `../REFACTOR/` | — | Target four-stage pipeline (`parse -> resolve -> canonical -> write`), the single resolution boundary, the native bundle format + Blender addon, and the full duplicated/divergent-path audit. Stages 1-4 built, the legacy glTF pipeline deleted; `../CANONICAL_FORMAT.md` (repo root) is the canonical `canon::`/bundle explainer for a reader outside this project | Own index and gate table at `../REFACTOR/README.md` |

## Suggested order, independent tasks only

`DPIV_TODO.md`'s corpus-statistics work is exhausted; its remaining step
is a human visual read. `TEXTURE_POOL_RECALL_TODO.md`'s ranking retune is
the single biggest independent lever left.

Everything else needs Luna's own interactive/client-side verification
(`BONE_CORRECTION_APPLICATION_TODO.md`, `PIXEL_SHADER_FORMULAS_TODO.md` item 2,
`TEXTURE_POOL_RECALL_TODO.md`'s full-corpus render item — this one bit a
previous session twice, see its own gate note there).
