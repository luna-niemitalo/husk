# TODO: read-only (RO) completeness for four 🚧 format-matrix rows

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `TODO_correctness.md`'s own convention) —
git history is the record of what was fixed and when, not this file.

Scope: the four specific 🚧 ("partially read") rows named directly by Luna,
out of the seven currently 🚧 in `README.md`'s format-support matrix (the
other three — LOD/mesh views, Interaction points, Lights — aren't in scope
here). Each item below was grounded against the actual current source and
`documentation/wowdev-wiki/md/M2.md` before being written down, not copied
from the README's own summary phrasing.

**A real mistake happened and was corrected while writing this file**: the
first draft of Item 3 claimed `WFV1`/`WFV2`/`DPIV`/`AFRA` were "confirmed
absent from the full local corpus" and treated the item as externally
blocked. That was wrong — it read only the first half of an already-
corrected finding in `WIKI_FINDINGS.md` §10 (a scanner bug, not a real
absence) and missed that the real files it turned up are sitting in this
repo's own root right now. See Item 3 below for the corrected version;
it's real, unblocked work, not a data-availability watch. Only Item 4
turned out to be a genuine non-task (a documented non-goal, not a
blocker) — that's a README symbol fix, not implementation work either.
Nothing in this file is "genuinely blocked" in the sense the first draft
claimed; Item 1 (`blp/`) is the only item with a real open empirical
question (whether a DXT3/JPEG BLP2 file exists at all) still worth
resolving before deciding whether to implement.

---

## 1. `blp/`: DXT3 and JPEG content decode

**Current state**: `husk-blp` (`blp/husk_blp/`) resolves the BLP2 header and
mip table and decodes palettized/DXT1/DXT5/BGRA content to PNG
(`README.md` lines 360-361, 543-564). DXT3 and JPEG remain unimplemented.

**Real blocker, not just unstarted work**: per `README.md`'s own existing
caveat, DXT3 is "unseen in this repo's real test data so far — not yet a
confirmed-needed gap," and JPEG content is independently noted as rare in
BLP2 per the wiki. This project's own methodology (verify against real
bytes before implementing, `WIKI_FINDINGS.md` throughout) means this
shouldn't be implemented blind — a decode path with no real file to check
it against is exactly the kind of unverified guess this codebase avoids
elsewhere (see `TODO_correctness.md` #4's texture-transform item for the
same discipline applied to a different feature).

**Plan**:
1. Scan the local corpus (`/media/luna/data/wow_export`, read-only, same
   `direnv exec . uv run --python tools/venv/bin/python <script>` pattern
   every prior corpus scan in this repo has used) for real `.blp` files
   whose header's `compression`/`alpha_encoding` fields indicate DXT3
   (compression 2, alpha depth 8, alpha encoding 1 — cross-check the exact
   field values against `blp/husk_blp/`'s own header parser rather than
   guessing) or JPEG (compression 0) content, independent of husk-blp's own
   parser, same as every other from-scratch verification scan in this repo.
2. If real files turn up: implement decode paths. DXT3 (BC2) needs the same
   synthetic-DDS-wrapper approach `decode.py` already uses for DXT1/DXT5,
   just a different `dxgi`/FourCC tag Pillow's DDS reader recognizes; JPEG
   needs BLP2's shared `jpeg_header` bytes (wiki-documented, prefixed to
   each mip's own JPEG data to reconstruct a valid standalone JPEG stream)
   decoded via Pillow directly. Test against real bytes, single-block
   fixtures first, matching `blp/tests/test_decode.py`'s existing
   precedent (see `README.md` lines 543-564 for the exact test-first
   discipline already established for DXT1/DXT5/palette).
3. If the scan comes back with zero real files for one or both (a real
   possibility — JPEG in particular is documented as rare): don't
   implement blind. Record the corpus-wide negative result explicitly
   (same disposition `WIKI_FINDINGS.md` §10 already gave WFV1/WFV2/DPIV/
   AFRA — "checked, zero real files, not a gap that can be closed without
   data") rather than leaving the README's current hedge sitting
   unconfirmed indefinitely.

**Priority**: low — cheap to resolve either way (a scan settles the
open question), but genuinely optional until/unless real files turn up.

---

## 2. Header / global metadata — two concrete, unblocked gaps

**Current state**: `husk info` (`src/cmd_info.cpp`) prints magic/version/
name, `global_flags` as a raw hex value only (line 115), and every
Wrath+-offset header field through `particle_emitters` (bounding boxes,
sphere radii, collision box, all array counts) — see `src/m2.cpp`'s
`offset::` table, which stops at `particleEmitters` (`src/m2.cpp:55`,
`minHeaderSize` at line 62). Unlike two other gaps this session's
grounding pass ruled out (blocked on external data), both items below are
directly actionable now — the wiki gives the authoritative bit list and
the conditional field's exact offset already, nothing to reverse-engineer.

### 2a. `global_flags` isn't decoded into its named bits

`documentation/wowdev-wiki/md/M2.md` (lines 33-68) documents `global_flags`
as a ~31-bit bitfield with real, named bits (not all resolved to a known
meaning, but named by hex value regardless):
`flag_tilt_x` (0x1), `flag_tilt_y` (0x2), `flag_use_texture_combiner_combos`
(0x8, ≥BC), `flag_load_phys_data` (0x20, ≥Mists — directly relevant to this
project's own `.phys`/`PFID` handling), `flag_unk_0x80` (≥WoD — "demon
hunter tattoos stop glowing" if unset), `flag_camera_related` (0x100),
`flag_new_particle_record` (0x200, ≥Legion — the exact flag that decides
`M2Particle`'s 476-vs-492-byte record size, which `src/m2.hpp`'s own
`kMinVerifiedParticleVersion` handling currently gates on *version* instead
— worth cross-checking whether version alone is a reliable proxy or
whether a real file exists where they disagree), `flag_texture_transforms_
use_bone_sequences` (0x800), `ChunkedAnimFiles_0x2000`, and eight more
`flag_unk_0x*` bits through `0x40000000` (seen on 11.1.7+ player-housing
furniture per the wiki's own annotation).

**Plan**: add a `globalFlagNames(uint32_t)`-style helper (mirroring the
existing `m2::billboardModeName` precedent, `src/cmd_info.cpp:127` —
same "named-bit → string" shape, not a new pattern) and print which named
bits are set alongside the existing raw hex, in `husk info`. Purely
additive/diagnostic — no parsing-pipeline or glTF-export change. Sanity-
check against a couple of real files (do any of `bloodelffemale.m2`/
`bloodelffemale_hd.m2`/the committed weapon fixtures set
`flag_load_phys_data` or `flag_new_particle_record`, and does that line up
with whether they actually carry `.phys` data or the newer/older particle
record size?) before trusting the decode, per this project's own standing
discipline.

### 2b. `textureCombinerCombos` — a real header array husk doesn't read at all

Already flagged honestly in-code: `src/m2.cpp:57-62`'s comment states
outright that the header continues past `particleEmitters`, "version-gated
(e.g. `textureCombinerCombos`, only present when a global flag bit is
set)," and that `minHeaderSize` is deliberately only "the minimum a header
must be for every field above to be safely readable, not the full struct
size." Per the wiki (`M2.md` lines 118-123): when
`flag_use_texture_combiner_combos` is set, an `M2Array<uint16_t>` sits at
offset 0x130 (right after `particle_emitters`), named "Second Texture
Material Override Combos" — per the wiki, when set, M2 multitexturing uses
*this* table's material instead of "current index material + 1" for
blending with the first texture. That's a direct, unimplemented piece of
the same multi-texture-layer story `README.md`'s Materials row already
partially covers via `extras` (`M2Batch.textureCount > 1` resolution,
`src/cmd_export.cpp`) — right now a batch with this flag set would still
resolve its second layer using the "index + 1" assumption, silently wrong
whenever a real file actually uses the override table instead.

**Plan**:
1. Scan the local corpus for real files with `flag_use_texture_combiner_
   combos` set (once 2a's decode helper exists, this is a one-line
   condition) — confirm the array is genuinely populated somewhere real
   before trusting the wiki's offset blindly, per usual.
2. Add `Header::textureCombinerCombos` (an `Array`, same shape as every
   other lookup-table field) and dereference it (`m2::parseUint16Array`
   already exists, reused by `dump-chunks`'s `PABC`/`PGD1` handling —
   `src/cmd_dump.cpp:230-239` — so no new decode primitive is needed).
3. Surface it: `husk info` for the raw count/values (same treatment as
   the five already-parsed-but-unreferenced lookup tables
   `TODO_correctness.md` #2 tracks — this would be a sixth), and cross-
   reference it from `cmd_export.cpp`'s existing second-texture-layer
   `extras` block so a batch under this flag gets the *correct* override
   material index in its extras instead of the "index + 1" assumption.
   Stays `extras`-only for the same reason the rest of that block does —
   core glTF has no combiner-blend-mode slot to translate into, this is
   about correcting *which* material FileDataID gets surfaced, not adding
   a new glTF representation.

**Priority**: high relative to the other three items here — both halves
are fully spec'd already (no wiki gap, no missing real-file blocker beyond
a quick corpus sanity-check), directly improve an existing feature's
correctness (2b), and are small, self-contained changes.

---

## 3. `MD20`/`MD21` chunk container — 4 tags real and present, struct-level parsing not yet attempted

**Correction, made in this same pass**: an earlier draft of this item
claimed `WFV1`/`WFV2`/`DPIV`/`AFRA` were "confirmed absent from the full
local corpus" and therefore externally blocked. That was wrong — it read
only the *first half* of `WIKI_FINDINGS.md` §10, which documents a real
scanner bug (`tools/find_m2_unknown_chunks.py` compared a `bytes` tag
against a `str`-keyed dict — `bytes == str` is always `False` in Python 3,
so the "0/130,576" result was a property of the bug, not the corpus) and
then the *correction*: `casc-tool`'s independently-written scanner (no
shared code, different language) found real hits for all five original
targets, and the exploration file lists are sitting right in this repo's
own root right now (`wfv1_files_for_exploration.txt` — 2 lines,
`wfv2_files_for_exploration.txt` — 2 lines, `afra_files_for_exploration
.txt` — 32 lines, `dpiv_files_for_exploration.txt` — 2,632 lines).
`PCOL` (the fifth tag in that same batch) was already fixed off the back
of this correction — see `M2_COMPLETENESS.md`/§10's own Follow-up
subsection — but `WFV1`/`WFV2`/`DPIV`/`AFRA` were not.

**Current state, checked directly against `src/cmd_dump.cpp`**: all 30
wowdev.wiki-documented top-level tags are *recognized*
(`documentedM2ChunkTags()`, `src/cmd_info.cpp:27-34`), but these 4 still
sit in `kFallback` (`src/cmd_dump.cpp:1511-1520`) — a raw hex dump plus a
note whose text is now stale (`AFRA`'s note still says "not observed in
any files yet," `WFV1`/`WFV2`'s still say the wiki's structure is simply
"unknown" as of a fetch that predates the correction). None of the four
get field-level parsing the way the other 18 documented/dumped tags do.

**Why this is a real reverse-engineering task, not a wiki-transcription
one**: unlike `PCOL` (which had at least a preliminary wiki struct to
verify against) or `DETL`/`WFV3` (byte-count discrepancies in an existing
struct), wowdev.wiki has **no struct at all** for any of these four —
this is the same starting position `.bone` was in (no wiki spec,
reverse-engineered purely from real bytes). `WIKI_FINDINGS.md` §10
already did real groundwork, not just counting: `AFRA` (16 bytes, all 32
hits) decodes cleanly as a real little-endian `float32` in [0,1] plus 12
zero bytes across every single hit — a strong, verified-real, single-field
hypothesis, not a guess; `DPIV` (32 bytes, all 2,632 hits) is "mostly
zero-filled," matching the wiki's own "mostly empty" text, with a minority
of real files having plausible non-zero leading floats. `WFV1`/`WFV2`
weren't byte-decoded at all yet beyond confirming their real file paths —
genuinely thin samples (2 files each, both the same Nazjatar zone), so
any struct derived from just two files should be flagged tentative, the
same "small sample, don't overclaim" caveat this project already applies
elsewhere (e.g. `WFV3`'s two-shape finding was still checked against all
9 of its real hits before being trusted).

**Plan**:
1. Fix `src/cmd_dump.cpp`'s stale `kFallback` note text regardless of
   whether the struct work happens next — "not observed in any files
   yet" is now simply false and shouldn't ship in `dump-chunks` output.
2. Byte-decode `DPIV` and `AFRA` first (best sample sizes, and `AFRA`
   already has a verified single-float hypothesis to build a real parser
   from) against every file `dpiv_files_for_exploration.txt`/
   `afra_files_for_exploration.txt` already lists, same from-scratch
   independent-script discipline `check_detl_stride.py`/
   `check_alias_next.py` used.
3. `WFV1`/`WFV2` next, explicitly caveated as a 2-file sample.
4. Implement `dumpWfv1`/`dumpWfv2`/`dumpDpiv`/`dumpAfra` (same shape as
   `dumpNerf`/`dumpEdgf`, `src/cmd_dump.cpp:399-430`) and move all four
   from `kFallback` to `kDocumented` once a struct is confirmed against
   real bytes, writing up each as a new `WIKI_FINDINGS.md` section
   following §10/§11's existing "current text / proposed addition /
   evidence" format.

**Priority**: real and actionable, not blocked — should be treated as a
peer of Item 2, not parked.

---

## 4. Sidecar FileDataID resolution (`SFID`/`AFID`/`BFID`/`PFID`/`SKID`/`TXID`)

**Current state, checked directly rather than assumed**: every one of the
six already has a real, working *local-filesystem* resolution path — this
row is further along than its README phrasing might suggest in isolation:
- `SFID`/`AFID`/`BFID`/`TXID` → `--skin-dir`/`--anim`/`--bones-dir`/
  `--textures <dir>` look for `<dir>/<FileDataID>.{skin,anim,bone,png}`
  (confirmed for `TXID` specifically at `src/cmd_export.cpp:908-918`,
  `1110-1163` — resolved to a real embedded PNG whenever `--textures`
  has a match, same as the other three).
- `PFID`/`SKID` → `--phys`/`--skel` auto-detect a same-basename `.phys`/
  `.skel` file next to the model (`src/cmd_export.cpp:1469-1483`,
  `1674-1707`, `1901-1916`) when left unset, independent of whether
  `PFID`/`SKID` itself is even present in the file — same three-state
  (`auto`/explicit-path/`none`) shape as the directory-based four.

**What's actually left is a documented non-goal, not an oversight — and on
reflection this row's 🚧 is simply the wrong symbol, not an open gap.**
True *WoW/CASC* path resolution (turning a FileDataID into the game's own
listfile-derived path) is explicitly out of scope, `DESIGN.md`'s Non-goals
— husk has no CASC/listfile access and never will at runtime, by design.
The actual workflow this tool is built for already assumes any static
CASC content husk needs (a `.skin`/`.anim`/`.bone`/`.phys`/texture) has
been extracted to a local directory *before* husk ever runs (Luna's own
point, and consistent with every other sidecar in this project — `--skin-
dir`/`--textures`/`--anim`/`--bones-dir` are all "you populate this
directory yourself" by design, never "husk goes and fetches it"). Given
that, "read" for this row can only ever mean "resolve a FileDataID to a
file in a directory the user already populated" — and that's fully
implemented for all six IDs. There's no further "RO completeness" work
left to schedule here; the CASC-resolution half isn't a deferred read, it's
not a read this tool is meant to perform at all.

**Recommended doc fix, not a code task**: `README.md`'s format matrix
row for "Sidecar FileDataID resolution" should move from 🚧 to 📖 (or a new
symbol if the matrix wants to distinguish "fully read, by design scoped to
local files" from "fully read, no further scope exists" — the matrix
doesn't currently have that distinction, worth a glance at whether any
other row needs it too, e.g. `.phys`/`.bone` content rows already read
"full" for the same reason). This isn't a new capability to build, just
correcting a row that undersells what's already there.

**One real, small, actionable item did fall out of grounding this row**
(already named in `CLAUDE.md`'s own Resume "Next step" bullet, restated
here so it has a tracked home instead of only living in a Resume note):
the resolvers' own failure messages (`resolveSkin` and its siblings for
`--anim`/`--bones-dir`/`--textures`) don't name the specific candidate
path or FileDataID they tried before giving up — a real usability/
diagnostics gap, and a direct miss against this project's own Foreign
Data policy ("on failure, always print expected and actual values",
`~/.claude/CLAUDE.md`). Worth fixing: when a `--skin-dir`/`--textures`/
`--anim`/`--bones-dir` lookup fails to find a match, the error/skip path
should name the exact `<dir>/<FileDataID>.<ext>` path it checked, not just
report "not found."

**Priority**: low — the resolution *capability* this row measures is
already complete short of the deliberate CASC non-goal; only the
diagnostics-message polish is real, unblocked work.

---

## Priority order

1. **Item 2** (header metadata) — fully unblocked, well-specified by the
   wiki, and 2b fixes a real (if narrow) export-correctness gap on top of
   being read-only completeness.
2. **Item 3** (`WFV1`/`WFV2`/`DPIV`/`AFRA` struct-level parsing) — real,
   unblocked, real files already sitting in this repo's root
   (`*_files_for_exploration.txt`), `AFRA`/`DPIV` already have a working
   byte-level hypothesis to build from. Corrected up from an earlier
   mistaken "blocked" disposition in this same file — see the item's own
   opening paragraph.
3. **Item 1** (`blp/` DXT3/JPEG) — cheap to resolve the open question
   (a corpus scan) even if the actual decode work ends up staying parked
   on "no real file yet."
4. **Item 4**'s README symbol correction (🚧 → 📖) plus its one small
   diagnostics polish (candidate-path naming in resolver failure
   messages) — the row is already functionally complete by design; this
   is a doc fix and a minor usability nicety, not a capability gap.
