# TODO: correctness &amp; usability gaps

**Status: an open punch list, not a historical record.** Fixed items get
removed outright rather than kept as `[Fixed]` noise — git history is where
the record of what was fixed and when lives, not a checked-in file.

---

## Read-pipeline correctness

### 1. Cameras (`M2Camera`) — low priority, explicitly deprioritized

`M2Camera` records are WoW's own *baked, model-relative* cinematic camera
paths (character-select rotating camera, some cutscenes, login-screen
framing) — fixed viewpoints authored for Blizzard's own UI/cinematic
contexts, not something a model needs to render correctly from an
arbitrary camera a custom engine already owns. Unlike billboarding,
nothing about normal model rendering depends on this data. Currently
count/offset-only in `husk info`; leave as-is unless the goal ever expands
to literally reproducing WoW's specific character-select/cinematic
screens.

### 2. `.bone` correction-set selection — resolved; application semantics tracked separately

Real DB2 chain confirmed and wired into `husk export --db2-dir/--dbd-dir/
--customization-choice-ids` (`src/chrcustomization_db2.hpp`/`.cpp`):
`ChrCustomizationChoiceID` → `ChrCustomizationElement.
ChrCustomizationBoneSetID` → `ChrCustomizationBoneSet.BoneFileDataID` — a
real customization choice resolves to a real `.bone` FileDataID and, when
that file was also given via `--bones-dir`, marks the matching
`CorrectionSet` with `selected_by_choice_ids` extras. Verified against
real local data (`README.md`'s `.bone` section has usage).

`ChrCustomizationOption`/`ChrCustomizationChoice` (needed to enumerate
real choices by name or pick a default automatically) is still 0 bytes in
the current local extraction — the caller must supply a real choice ID
directly, same as `--char-layout-id` requires for
`CharComponentTextureLayoutsID`.

**Genuinely open, tracked separately**: applying the resolved correction
matrix to real Blender rendering — not a format wall
(`tools/husk_blender_geoset_mask.py` already applies other husk extras to
real rendering elsewhere), but the matrix's own application semantics
(multiply order, local-vs-model space) were never verified against real
client behavior. See `TODO/BONE_CORRECTION_APPLICATION_TODO.md`.
