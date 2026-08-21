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
| `PIXEL_SHADER_FORMULAS_TODO.md` | Filling wowdev.wiki's 17 undocumented `Combiners_*` formulas | A promising but unverified lead found (`reference/wow.export`'s shader); step 1 (find real corpus repros) done 2026-08-14 — 14/17 have real repros; a full corpus-wide invariant/equivalence-testing pass (2026-08-20, `references/wow_shaders/combiner_hunt/`) verified 3 formulas exact, found 3 more real-but-ambiguous, 4 genuine negatives (incl. `Dual_Crossfade`/`Masked_Dual_Crossfade`, resolved via a new external-weight-scalar search), plus a weak-signal lead set for `Illum` (new constant-output test tier) | Step 2 (final verification against real rendered output): human-gated |
| `RENDER_QUALITY_TODO.md` | Corpus-review render-quality findings (rotation, textures, alpha, billboards) | Rotation shear + V-scroll direction + `alphaCutoff` fixed (2026-08-14); 68 "unexplained" blank renders downgraded 2026-08-14 (6 spot-checked, 0 real bugs found); Mod/Mod2x multiply-blend compositing implemented and verified 2026-08-15 (`MOD_BLEND_COMPOSITING_TODO.md` closed and deleted); billboard ground-truth, ambiguous-pool tiebreak still open | Billboard ground-truth is human-gated; ambiguous-pool tiebreak independent |
| `CHAR_TEXTURE_COMPOSITING_TODO.md` | Full DB2-driven character texture compositing (base/overlay skin layers) | Stages 1-3 done: WDC5 parsing, real placement geometry (`--char-layout-id`), model-identity derivation (`--chr-model-id auto`), the real `ChrCustomizationMaterial → TextureFileData` FileDataID chain (`chr_enabled_materials` extras). Stage 4 (real pixel compositing) was built, verified, then deliberately reverted — pixel compositing isn't husk's job, Blender's own shader nodes are the right layer. Stage 5 (Blender-side node-graph tooling, live per-option choice switching) is now implemented, see `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`. Stage 6 (equipped-gear appearance, `husk appearance-string --db2-dir/--dbd-dir`) done 2026-08-21 — real `ItemModifiedAppearance → ItemAppearance → ItemDisplayInfo → ItemDisplayInfoModelMatRes` chain resolves `gear` entries to real model/texture FileDataIDs, verified end to end | Independent — attaching/rendering the resolved gear is the only remaining piece, Blender-side, not started |
| `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` | Blender-side live customization-choice texture switch (`CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 5) | Implemented — `tools/husk_blender_geoset_mask.py`'s new `apply_customization_texture_switch` (Value-node + Math(COMPARE)-gated Mix chain, no Menu Switch node exists in shader trees), verified structurally + a clean Cycles evaluation against a real DB2 export with placeholder textures. Still open: a real interactive GUI pass with real per-choice texture bytes | Independent to keep implementing; final visual pass is human-gated |
| `KNOWLEDGE_BASE_DESIGN.md` | A husk-owned DB2/listfile knowledge-base design, plus the object-skin-texture-resolution fix it grew out of | Object-skin texture resolution fixed 2026-08-16 — not via the DB2/knowledge-base chain (too collision-prone, kept disabled) but via a new local race/gender-suffix fallback tier in the existing fuzzy-basename matcher; only remaining step is running the real full-corpus render to completion and a visual spot-check | **Human-gated** — a previous session shot itself in the leg here twice (an unauthorized `rm`, an inline/foreground full-corpus render); get Luna's go-ahead before starting the render, and the visual spot-check itself needs her eyes, not self-certification |
| `BONE_CORRECTION_APPLICATION_TODO.md` | Applying `.bone` correction matrices in Blender, now that selection is resolved | Selection done (2026-08-14); application semantics (multiply order, space) never verified against real client behavior | **Human-gated** — needs a real side-by-side comparison, same as billboard alignment did |
| `ENGINE_TODO.md` | External-data gaps, and which are actually husk's to close (renumbered 2026-08-14 after former items 1/2 resolved+removed) | #1 hardcoded texture resolution (biggest remaining item, tracked in `CHAR_TEXTURE_COMPOSITING_TODO.md`); #2 `aliasNext` names checked and closed 2026-08-14 (local DB2 schema dropped `Name` around 7.3.5, unrecoverable); #3 `blendTimeOperation` (no data, needs a heuristic); #4 sound linking (unconfirmed); #5 LOD thresholds (a design decision, not a gap) | #4: independent investigation. #3: independent (author a heuristic). #5: a decision, not a task |
| `BONE_NAME_DEDUCTION_TODO.md` | Tier-2 bone naming (reference-skeleton matching) | Not started; cosmetic only, zero visual/render impact | Independent |
| `CLEANUP_TODO.md` | Code hygiene / follow-up punch list | Empty — comment-hygiene sweep closed 2026-08-20; the dangling-internal-reference corpus scan (`tools/corpus_scan_tasks/dangling_references_task.py`) closed 2026-08-21, real corpus result: every M2-only lookup kind 100% clean (1.35M+ references), `.skin`-dependent kinds low real rate (0.06%-0.37%), concentrated in the already-known recolor-variant class, no casc-tool follow-up warranted | N/A — nothing open |
| `DPIV_TODO.md` | Cracking the `DPIV` mystery chunk's real field semantics | Structural shape characterized (multi-record, ground-anchor-shaped); `field3`'s real meaning still open | Independent (corpus/byte analysis), but world/doodad-placement-adjacent, not core M2 rendering |
| `TODO_correctness.md` | Misc correctness/usability gaps punch list, including the `tools/live_gallery` browser-viewer verification follow-up | Camera support deprioritized by design; `.bone` slot *selection* now resolved (2026-08-14) — application is `BONE_CORRECTION_APPLICATION_TODO.md`'s job, not this file's; live-viewer curve playback structurally verified, not yet visually confirmed in a real browser; `resolveFieldString`'s multi-section string-offset bug fixed 2026-08-20; #2's name-enumeration/default-choice heuristic now implemented too (2026-08-20, `--chr-model-id`) | N/A — mostly closed or delegated elsewhere |
| `WORLD/` | WMO/ADT/world-geometry scope (12 files) — a separate project phase, own entry point `../WORLD_COMPLETENESS.md` | Investigated/planned, no code started | Not covered by this index — see `WORLD_COMPLETENESS.md` |

## Suggested order, independent tasks only

`CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stages 1-6 are all now done (real
DB2-driven compositing, the live Blender-side switch, and equipped-gear
appearance resolution, all verified end to end; Stage 5's own real-texture
interactive pass is human-gated, see its own file). `CLEANUP_TODO.md` is
empty. `DPIV_TODO.md`'s own concrete next steps (field2/field3 corpus
analysis) are independent, corpus/byte-analysis work.

Everything else needs Luna's own interactive/client-side verification
(`BONE_CORRECTION_APPLICATION_TODO.md`, `RENDER_QUALITY_TODO.md`'s
billboard ground-truth pass, `PIXEL_SHADER_FORMULAS_TODO.md` step 2,
`KNOWLEDGE_BASE_DESIGN.md`'s full-corpus render — see its own gate note
above, this one bit a previous session twice).
