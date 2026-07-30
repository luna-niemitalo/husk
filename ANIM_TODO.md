# `--anim` basename-fallback resolution — implementation TODO

**Status: root-caused and fully planned, not yet implemented.** Replaces
`ANIM_AFSB_FIXTURE_GAP.md` (deleted — that file's job, finding and diagnosing
the gap, is done; this file is the actionable follow-up, same lifecycle
`MULTIROOT_SKELETON_TODO.md`/`PHYS_TODO.md` used before their own
implementation passes). Self-contained: everything needed to implement this
is below, no other document needs to be read first.

## The gap, in one paragraph

`--anim`'s resolution (`buildAnimations`, `src/cmd_export.cpp`) only ever looks
for `<animDir>/<FileDataID>.anim`. A real `wow.export`-style extraction names
external animation files `<model-basename><animId zero-padded to 4>-<subAnimId
zero-padded to 2>.anim` instead (confirmed: the committed
`test_data/character/bloodelf/female/bloodelffemale_hd0069-00.anim` /
`-01.anim` files match this pattern exactly, and zero bare `<FileDataID>.anim`
files exist anywhere in the fixture set). `--skin` already has an equivalent
same-basename fallback (`findSameBasenameSkins`); `--anim` has none. Net
effect: a real user who extracts a character model and points `--anim` at it
exactly as the README's own worked example shows gets **zero** of the
genuinely-external animation sequences, silently — no error, no warning,
just fewer clips than expected. The AFSB/AFM2 *decode* logic itself is sound
and independently verified (`WIKI_FINDINGS.md` §2) — this is purely a
file-resolution gap, not a parsing bug.

## Current code (verified against source, not assumed)

`M2AnimInputs` — `src/cmd_export.cpp:264-273`:
```cpp
struct M2AnimInputs {
    std::optional<std::vector<m2::Header::AnimFileEntry>> animFileIds;
    bool animChunked = false;
    std::string animDir;
};
```

`findAnimFileId` — `src/cmd_export.cpp:278-286` (unchanged by this fix):
```cpp
uint32_t findAnimFileId(const std::vector<m2::Header::AnimFileEntry>& animFileIds, uint16_t animId,
                         uint16_t subAnimId) {
    for (const auto& e : animFileIds) {
        if (e.animId == animId && e.subAnimId == subAnimId && e.fileId != 0) {
            return e.fileId;
        }
    }
    return 0;
}
```

`buildAnimations`'s external-sequence branch — `src/cmd_export.cpp:466-479`
(the rest of the branch, `AFSB`-vs-`AFM2` chunk selection at ~482-505, is
**unchanged** by this fix):
```cpp
} else {
    if (animInputs.animDir.empty() || !animInputs.animFileIds) {
        continue;
    }
    uint32_t fileId = findAnimFileId(*animInputs.animFileIds, seq.id, seq.variationIndex);
    if (fileId == 0) {
        continue;
    }
    auto animPath =
        std::filesystem::path(animInputs.animDir) / (std::to_string(fileId) + ".anim");
    std::ifstream f(animPath, std::ios::binary);
    if (!f) {
        continue;  // not available locally -- same skip policy as --textures
    }
    // ... AFSB/AFM2 chunk selection, unchanged ...
}
```

`buildAnimations` is called from two sites, both of which already have
`modelPath` in scope (the enclosing `exportGlb`'s own parameter):
- Inline-bones path: `src/cmd_export.cpp:1454-1460`
  (`M2AnimInputs animInputs; animInputs.animFileIds = header.animFileIds; ...`)
- `.skel`-sourced path: `src/cmd_export.cpp:1486-1493`
  (`M2AnimInputs animInputs; animInputs.animFileIds = skel::findAnimFileIds(skelBytes); ...`)

`--anim`'s CLI option — `src/cmd_export.cpp:1206-1211`:
```cpp
app.add_option("-a,--anim", opts.animArg,
                "'auto': inline + global-sequence + best-effort external directory search; "
                "'inline': inline + global-sequence only, no external search; 'none': no "
                "animation clips at all (bind pose only); or a directory of "
                "'<FileDataID>.anim' files")
    ->capture_default_str();
```

`--anim`'s four-state resolution (already existing, **unchanged** by this
fix — `auto` already resolves `animDir` to the model's own directory, so the
fix applies to the default invocation, not just explicit `--anim <dir>`) —
`src/cmd_export.cpp:1302-1310`:
```cpp
std::string animDir;
bool animNone = false;
if (opts.animArg == "auto") {
    animDir = modelDirStr;
} else if (opts.animArg == "none") {
    animNone = true;
} else if (opts.animArg != "inline") {
    animDir = opts.animArg;
}
```

`main.cpp`'s completion-generator tables (`bashValueCompletion`/
`zshValueAction`/`zshFlagLabel`) already have `--anim` entries completing
`auto`/`inline`/`none` + directories — **this fix introduces no new CLI-visible
state or word**, so those tables do not need changes.

## Implementation plan

### 1. Add `modelPath` to `M2AnimInputs`

`src/cmd_export.cpp:264-273` — add a field and update the doc comment
(currently describes only the FileDataID convention):

```cpp
struct M2AnimInputs {
    std::optional<std::vector<m2::Header::AnimFileEntry>> animFileIds;
    bool animChunked = false;
    std::string animDir;
    std::string modelPath;  // for the same-basename fallback below
};
```

Set it at both call sites: `animInputs.modelPath = modelPath;` added to the
block at `cmd_export.cpp:1456-1459` and the block at `cmd_export.cpp:1488-1491`.
In both cases this is the model's own `.m2` path (the function parameter
already named `modelPath` in `exportGlb`) — **not** the `.skel`'s own path,
even in the `.skel`-sourced branch. Verified against the real fixture: the
model is `bloodelffemale_hd.m2`, the `.skel` is `bloodelffemale_hd.skel`
(same basename in this fixture, but the naming convention that matters is the
*model's*, matching what a real extraction names its `.anim` files after —
the `.m2`, not any sidecar).

### 2. New `findAnimFileByBasename`, next to `findAnimFileId`

Insert after `findAnimFileId` (`src/cmd_export.cpp:286`):

```cpp
// Zero-pads `value` to at least `width` digits (e.g. zeroPad(69, 4) == "0069").
std::string zeroPad(unsigned value, size_t width) {
    std::string s = std::to_string(value);
    if (s.size() < width) s.insert(0, width - s.size(), '0');
    return s;
}

// Real wow.export-style extractions name external .anim files
// <model-basename><animId:04d>-<subAnimId:02d>.anim next to the model, not
// by FileDataID (see ANIM_TODO.md for the real-corpus evidence: the
// committed bloodelffemale_hd0069-00.anim/-01.anim fixtures match this
// exactly, and no bare <FileDataID>.anim file exists anywhere in the real
// corpus sample). Direct filename construction, not a directory scan like
// findSameBasenameSkins -- (animId, subAnimId) fully determines the name, so
// there's no ambiguity to resolve the way .skin's open-ended LOD-suffix scan
// has. Returns the constructed path unconditionally (existence is checked by
// the caller's own ifstream-open attempt, same as the FileDataID path).
std::filesystem::path findAnimFileByBasename(const std::string& modelPath,
                                              const std::string& animDir, uint16_t animId,
                                              uint16_t subAnimId) {
    std::string baseName = std::filesystem::path(modelPath).stem().string();
    std::string fileName = baseName + zeroPad(animId, 4) + "-" + zeroPad(subAnimId, 2) + ".anim";
    return std::filesystem::path(animDir) / fileName;
}
```

### 3. Rewire `buildAnimations`'s external branch

Replace `src/cmd_export.cpp:466-479` with:

```cpp
} else {
    if (animInputs.animDir.empty()) {
        continue;
    }
    // FileDataID-named file first (primary -- some extraction tools do use
    // this convention); same-basename convention second (the real
    // wow.export-shaped fallback, see findAnimFileByBasename). Neither
    // requires the other: an AFID-less model/.skel (animFileIds ==
    // std::nullopt) skips straight to the basename attempt below, rather
    // than skipping external resolution outright the way this branch used
    // to.
    std::filesystem::path animPath;
    if (animInputs.animFileIds) {
        uint32_t fileId = findAnimFileId(*animInputs.animFileIds, seq.id, seq.variationIndex);
        if (fileId != 0) {
            animPath =
                std::filesystem::path(animInputs.animDir) / (std::to_string(fileId) + ".anim");
        }
    }
    std::ifstream f;
    if (!animPath.empty()) {
        f.open(animPath, std::ios::binary);
    }
    if (!f) {
        animPath = findAnimFileByBasename(animInputs.modelPath, animInputs.animDir, seq.id,
                                           seq.variationIndex);
        f.open(animPath, std::ios::binary);
    }
    if (!f) {
        continue;  // not available locally under either naming convention
    }
    // ... AFSB/AFM2 chunk selection, unchanged from here ...
}
```

Everything from the existing `if (animInputs.animChunked) { ... }` block
onward (`~482-505` currently) is untouched — it already operates on
`animFileBytes`, read from whichever `f`/`animPath` was settled on above.

### 4. Update `--anim`'s CLI help text

`src/cmd_export.cpp:1206-1211` — the `"or a directory of '<FileDataID>.anim'
files"` clause should become something like `"or a directory of
'<FileDataID>.anim' files (falling back to '<model-basename><animId>-
<subId>.anim' when a FileDataID-named file isn't found)"` — concise enough to
fit CLI11's help output, mirroring how `--skin`'s own help text documents its
two-stage resolution.

## Test plan

### New `tests/test_cli.cpp` cases

Place these alongside the existing three AFSB/AFM2 cases
(`tests/test_cli.cpp:1072`, `:1132`, `:1191` — same `buildSkb1PayloadForTracks`/
`appendChunkTo`/`buildSks1Payload`/`putArrayAt`/`tempPath` construction style,
not the `buildSkel` helper those three also skip).

1. **Basename fallback resolves when there's no `AFID` entry for the
   sequence at all** (covers the AFID-absent case from this session's
   planning pass) — build a `.skel` with a real `SKB1`/`SKS1`-declared
   external sequence (e.g. id=1234, variationIndex=0) and **no `AFID` chunk**
   at all (`animFileIds` resolves to `std::nullopt`); write
   `<model-stem>1234-00.anim` (a valid `AFSB`-chunked file) into the anim
   directory. Assert `exitCode == 0` and `"1 animation(s)"` in output.
2. **Basename fallback used when an `AFID` entry exists but its
   FileDataID-named file is missing** — `AFID` maps (id=1234, var=0) →
   fileId=777, but no `777.anim` exists; `<model-stem>1234-00.anim` does.
   Assert the clip still resolves (`"1 animation(s)"`).
3. **FileDataID file takes priority when both exist** — `AFID` maps to
   fileId=777; write both `777.anim` (valid `AFSB` chunk — resolves) *and*
   `<model-stem>1234-00.anim` (deliberately malformed, e.g. a bare `ZZZZ`
   chunk that would produce `"bind pose only, no animation"` if it were the
   one actually read). Assert output shows `"1 animation(s)"` (proves the
   FileDataID file won, not the malformed basename one — distinguishing
   priority by output shape rather than needing keyframe-value inspection,
   consistent with how the three existing AFSB tests already assert).
4. **Neither naming convention resolves → no clip, not an error** — `AFID`
   maps to a fileId with no matching file, and no basename-matching file
   exists either. Assert `exitCode == 0` and `"bind pose only, no
   animation"` (mirrors the existing "neither chunk present" case's
   assertion shape, but for the file-not-found case instead of the
   chunk-not-found case).

### `tests/test_integration.cpp` — strengthen the real end-to-end case

The existing case at `tests/test_integration.cpp:205-279` (gated by
`doctest::skip` on `testAnimDir()` etc. being non-empty) currently only
checks `CHECK(model.animations.size() > 100)` — true today from inline +
global-sequence clips alone, so it doesn't actually prove the external-file
path ran. Add, right after that count check:

```cpp
bool foundAnim69_0 = false;
bool foundAnim69_1 = false;
for (const auto& anim : model.animations) {
    if (anim.name == "anim_69_0") foundAnim69_0 = true;
    if (anim.name == "anim_69_1") foundAnim69_1 = true;
}
CHECK(foundAnim69_0);
CHECK(foundAnim69_1);
```

These two names are exact and known in advance: the committed
`bloodelffemale_hd0069-00.anim`/`-01.anim` fixtures are `animId=69`,
`variationIndex=0` and `1`, and `buildAnimations` names every clip
`"anim_" + id + "_" + variationIndex` (`cmd_export.cpp:513`, unchanged). This
is the concrete proof the fallback path resolved real, correctly-named,
on-disk files through husk's actual `--anim` CLI mechanism — not just a
loose count.

### Full local verification (once implemented)

- `direnv exec . cmake --build build && ./build/husk-tests` — full suite green,
  including the 4 new `test_cli.cpp` cases and the strengthened
  `test_integration.cpp` case (the latter only runs if `testAnimDir()`
  resolves — it does, against the committed fixtures, no env var override
  needed).
- `ctest` — same, confirming the `HUSK_TEST_DATA_DIR`-baked absolute-path
  behavior still holds.
- Re-run `husk export` by hand on `bloodelffemale_hd.m2` with `--anim`
  pointed at its own directory (the `auto` default) and confirm the reported
  animation count increases beyond 336 (by up to 2, matching the two real
  external `.anim` fixtures now committed) — the concrete, human-observable
  proof the fix does something on the one real fixture available.

## Doc-sync checklist (once implemented and verified)

- `DESIGN.md`: CLI grammar table row for `--anim` (currently `| --anim | -a |
  directory of <FileDataID>.anim, or one of auto/inline/none (see below) |`)
  — note the basename fallback. The AFSB design note (Key design decisions,
  the paragraph ending in "...336 real clips... 336 actions, matching
  exactly") should gain a sentence: that claim verified the *decode*, not
  husk's own `--anim` CLI resolution of real basename-named files — this fix
  is what makes that true end-to-end, and the strengthened integration test
  is the new proof.
- `WIKI_FINDINGS.md` §2 follow-up, point 6 (currently claims "336 real
  animation clips... verified three independent ways" as proof of real
  end-to-end `--anim` resolution) — correct to note the actual split (335
  inline + 2 global-sequence + 54 genuinely-external, of which 0 resolved
  via husk's CLI before this fix, and only the 2 present in the current
  pruned fixture set can be end-to-end-verified after it) and that the
  "verified against all 104 real `.anim` files" claim was a separate script
  reading files directly, not through `--anim`'s own lookup.
- `README.md`: `--anim` usage section — add a sentence describing the
  same-basename fallback, mirroring the existing `--skin` two-stage
  description.
- `CLAUDE.md`'s `## Resume` — add a new "Last state" entry per this
  project's established convention once implemented (not spelled out
  mechanically here; follow the shape of the existing entries).
- Once every item above has a final disposition, **delete `ANIM_TODO.md`
  outright** (don't leave it marked `[DONE]`) — same "survey's job is done"
  lifecycle `CORPUS_TODO.md`/`VERIFICATION_IDEAS.md`/
  `MULTIROOT_SKELETON_TODO.md` already went through. Repoint any
  `ANIM_TODO.md`-referencing comments introduced during implementation (e.g.
  the `findAnimFileByBasename` doc comment above) to `DESIGN.md`/
  `WIKI_FINDINGS.md` instead, the same way those three deletions repointed
  their own live cross-references.

## Explicitly out of scope

`TODO_correctness.md` #4 (alias sequences, `M2Sequence.flags & 0x40`, 7 of
396 real sequences, currently skipped with `// wowdev.wiki: "I have no clue"
where this lives.`, `cmd_export.cpp:464-465`) is a separate, still-open
question, unrelated to this fix — cross-referenced here, not folded in. Do
not attempt to resolve it as part of implementing this document.
