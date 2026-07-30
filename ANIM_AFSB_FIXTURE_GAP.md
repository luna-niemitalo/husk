# `--anim` real-fixture gap: husk's own resolution is missing the naming convention real users actually have

**Status: investigated and root-caused this session, not yet fixed.** Found
while pruning `test_data/` down to a minimal fixture set (unrelated task).
No `src/` changes made yet. This file is the record; see "Recommended fix"
for what's left.

**Revised framing, corrected mid-investigation.** My first pass at this
concluded the fix was "rename the committed `.anim` fixtures to their
FileDataIDs so they match what husk already looks for." That's backwards,
and was called out directly: **the human-readable names
(`bloodelffemale_hd0069-00.anim`) are the canonical, real-world artifact —
they're what `wow.export`-style extraction actually produces, and what any
real user pointing `--anim` at their own extraction directory would have.**
Nobody has a directory of bare `<FileDataID>.anim` files sitting around;
that's not a real extraction shape, it's just what husk's own lookup
currently expects. The gap is in husk's `--anim` resolution, not in the
fixtures. This file now reflects that correction throughout.

**Short answer up front: don't worry about decode correctness, do fix
`--anim`'s resolution.** The AFSB decoder is sound (see "Why the decode
logic itself is still trustworthy" below, unchanged from the original
investigation). What's actually missing is a same-basename fallback for
`--anim`, mirroring the one `--skin` already has — without it, a real user
following the README's own worked example (extract a character's `.anim`
files next to its `.m2`/`.skel`, point `--anim` at that directory) gets
**silent, total failure of every externally-stored sequence**, no error,
no warning, just fewer clips than expected.

---

## The claim, as currently documented

`WIKI_FINDINGS.md` §2's follow-up and `CLAUDE.md`'s "AFSB cracked" session
both say, in effect: *husk exports `bloodelffemale_hd.m2` + `.skel` + the
real `--anim` directory and gets 336 real animation clips, up from ~0
external ones possible before — proof the AFSB crack works end-to-end
against real data.* `tests/test_integration.cpp`'s
`"husk export: a .skel-sourced model's external AFSB-shaped .anim files
resolve real, sane (unit-norm, finite) animation clips, end to end"` test
case asserts `model.animations.size() > 100` against exactly this fixture
and directory, and is presented as the automated proof of that claim.

## What's actually true

`bloodelffemale_hd.skel`'s `SKS1` chunk declares **396 sequences**. Their
`M2Sequence.flags` field (byte offset `+0x0C` within each 64-byte record —
see `src/m2.cpp`'s `parseSequences`) splits three ways:

| Classification | Flag bits | Count | Data source |
|---|---|---|---|
| **Inline** | `0x20` set | **335** | `.skel`'s own `SKB1` payload directly — no external file involved, ever |
| **Alias** | `0x40` set (and not `0x20`) | 7 | Skipped outright — see `TODO_correctness.md` #4, added this session, for why "unresolvable" needs its own re-check |
| **Genuinely external** | neither bit set | **54** | Requires resolving `seq.id`/`seq.variationIndex` through the `.skel`'s `AFID` table to a real `FileDataID`, then reading an external `.anim` file — this is the actual AFSB code path |

`husk export`'s reported "336 animation(s)" is **334 inline clips + 2
global-sequence clips** (`global_seq_0`, `global_seq_1` — a completely
separate mechanism, `buildGlobalSequenceAnimations`, unconditional and
unrelated to `SKS1` entirely). Verified by extracting the glTF JSON chunk
from the exported `.glb` and checking animation names directly: 334 named
`anim_<id>_<var>`, 2 named `global_seq_<n>`, zero of the 54 known-external
`(id, var)` pairs (e.g. `anim_69_0`) present anywhere.

**None of the 54 genuinely-external sequences currently resolve, at all**,
through the committed `test_data/character/bloodelf/female/` directory —
or through *any* directory of real, `wow.export`-shaped `.anim` files.
Proof: `husk export` on this fixture produces **exactly 336 clips** whether
`--anim` points at the real committed directory, an empty directory, a
nonexistent path, or is omitted entirely.

## The human-readable names are complete and correct — confirmed, not assumed

Cross-referenced the full original 54-file `bloodelffemale_hd*.anim`
corpus (before this session's unrelated pruning) against the 54 sequences
`.skel`'s own `SKS1` flags mark as genuinely external: **exact 1:1 match,
both directions** — every external `(id, variationIndex)` pair has exactly
one corresponding `bloodelffemale_hd<id:04d>-<variationIndex:02d>.anim`
file, and every one of those files corresponds to exactly one external
sequence. Nothing missing, nothing extra. This is what a complete,
correct, real extraction of this model's external animation data looks
like — it's exactly the shape `wow.export`-style tooling actually
produces, not a mislabeled or partial set.

## Why: husk's `--anim` resolution only tries one naming convention, and it isn't this one

`buildAnimations` (`src/cmd_export.cpp:474-475`) resolves an external
sequence by looking for `<animDir>/<FileDataID>.anim` only — a pure
decimal number, the same "local-directory-by-FileDataID" convention
`--textures`/`--bones-dir` use. But unlike those two, **`--skin` already
has a second resolution stage for exactly this situation**:
`findSameBasenameSkins` (`src/cmd_export.cpp:1077`) falls back to scanning
for `<model-basename><NN>.skin` next to the model whenever the
FileDataID-named file isn't found in `--skin-dir` — because that's the
naming shape a real extraction commonly produces instead. `--anim` has no
equivalent fallback at all: FileDataID or nothing.

The real-world `.anim` naming convention is just as mechanical as `.skin`'s
own — `<model-basename><animId zero-padded to 4><-><subAnimId zero-padded
to 2>.anim` — and the `(animId, subAnimId)` pair needed to construct it is
exactly `seq.id`/`seq.variationIndex`, already in hand at the point
`buildAnimations` currently gives up and looks only for the FileDataID
name. **This is a real, fixable gap in husk's own `--anim` flag, not a
test-data problem.**

## Why the decode logic itself is still trustworthy

Two things establish this, independently of the resolution gap above:

1. **The original investigation's byte-level verification read the real
   `.anim` files directly**, by their real (human-readable) on-disk names,
   via a separate Python script — never through husk's own `--anim`
   file-lookup mechanism at all. `WIKI_FINDINGS.md` §2's "self-consistency
   sweep across all 54 non-`_sdr` `bloodelffemale_hd*.anim` files" — that
   number, 54, is exactly the count of genuinely-external sequences found
   above, not a coincidence. That sweep (monotonic in-bounds timestamps,
   unit-length quaternions, finite values, offsets landing exactly on
   `AFSB` timestamp runs) is real evidence the *decode* is correct; it
   just didn't go through the CLI's file-resolution step, so it never
   would have caught the missing fallback.
2. **`tests/test_cli.cpp` unit-tests the AFSB chunk-selection logic with
   synthetic fixtures** (`buildSkel`/`tempPath`-constructed files, named to
   match husk's current FileDataID lookup by construction) — the "plain
   AFSB-only file" and "AFM2 stub alongside real AFSB data, AFSB wins"
   cases are real coverage of the decode/chunk-selection logic, unaffected
   by the resolution gap described here.

What has **zero** current automated coverage is the *combination*: real
corpus `.skel`+`.anim` bytes, named the way a real extraction actually
names them, resolved through husk's actual `--anim` CLI mechanism, end to
end. That's exactly what the integration test's name claims to cover, and
exactly what would fail today if it tried.

## Should you be worried?

**Not about the decoder.** Nothing about `resolveVec3TrackSequence`/
`resolveQuatTrackSequence`/the `AFSB`-vs-`AFM2` chunk selection needs to
change; both are independently verified (previous section).

**Yes, mildly, about real usability.** `--anim`'s current FileDataID-only
resolution means: a real user who extracts a character model with
`wow.export` or similar, gets a directory of `<basename><animId>-
<subId>.anim` files (the actual, complete, correct data — confirmed
above), points `--anim` at it exactly as the README's own worked example
shows — and gets **zero** of the genuinely-external sequences, silently.
No error, no warning; `buildAnimations`'s skip path is the same graceful
"not available locally" policy `--textures` uses for a missing PNG, which
is the right policy for an *optional* enrichment but the wrong one here
since it's currently swallowing 100% of a real, present, correctly-named
input. For `bloodelffemale_hd.m2` specifically the loss is disguised by
the 335 inline clips still working fine; a model with a higher external
fraction would show a much more visible gap.

## Recommended fix

**Give `--anim` the same two-stage resolution `--skin` already has**,
mirroring `findSameBasenameSkins`'s shape: FileDataID first (via the
`.skel`'s/model's own `AFID` table, current behavior, kept as the primary
path — still correct for the extraction tools that do use FileDataID
naming), falling back to `<model-basename><seq.id:04d>-
<seq.variationIndex:02d>.anim` next to the model (or in the given
`--anim` directory) when the FileDataID-named file isn't found. Concretely:

1. New function alongside `findAnimFileId`, something like
   `findAnimFileByBasename(modelPath, animDir, seq.id, seq.variationIndex)`
   — construct the candidate path, check existence, same shape
   `findSameBasenameSkins` already established (just a direct filename
   construction here rather than a directory scan, since the convention is
   fully mechanical once `id`/`variationIndex` are known — no ambiguity to
   resolve the way `.skin`'s LOD-suffix scan has).
2. In `buildAnimations`'s external branch (`src/cmd_export.cpp:466-509`):
   try the FileDataID path first (current behavior); if that doesn't
   exist, try the basename-convention path before falling through to
   `continue`.
3. **No committed fixture renaming needed** — the existing
   `bloodelffemale_hd0069-00.anim`/`-01.anim` files (kept from the prune)
   already exercise this path correctly once the fallback exists; the
   `test_data_paths.hpp`/`test_data/` layout doesn't need to change at all.
4. Strengthen `test_integration.cpp`'s assertion the same way the earlier
   version of this document already recommended: a specific `CHECK` that a
   known-external clip (e.g. `anim_69_0`) is present, not just a loose
   `> 100` count — this is what actually proves the fallback path ran,
   and would have caught this gap the day the corpus was first committed.
5. `src/main.cpp`'s doc comment / `--anim`'s own CLI help text
   (`app.add_option("-a,--anim", ...)`, `cmd_export.cpp:1206`) should
   mention the fallback explicitly, the same way `--skin`'s own help text
   already documents its two-stage resolution — so this is discoverable
   from `--help` alone, not just from reading source.
6. A short correction to `WIKI_FINDINGS.md` §2's follow-up (point 6),
   noting the 335/54/7 split and that "336 real clips" wasn't, on its own,
   proof the external-file path was exercised — for accuracy, not because
   the underlying decode finding was wrong.

**Per this project's own established practice** (`DESIGN.md`'s stated
convention, followed for `--bones-dir` and the particle/ribbon work): a
change to the `--anim` resolution pipeline — CLI-adjacent, affects a
documented flag's behavior — should go through a real plan-mode design
pass before implementation, not just get typed in. Flagging that step as
still outstanding; nothing above has been implemented.
