# M2_GAPS_TODO — implementation plan for documented-but-unbuilt M2 coverage

**Status: ready to implement, item by item.** Unlike `PHYS_TODO.md`'s
situation (one cohesive feature, one investigation, one plan), this file
bundles several independent, small-to-medium gaps a coverage review this
session found — each one has a fully documented byte layout (wowdev.wiki,
or husk's own already-verified parsers for a sibling format) and no
external-data blocker. That's the dividing line that put something here
rather than in `TODO_correctness.md` (genuinely low-priority-by-design, or
blocked on client-side DB2 data) or the new `M2_UNKNOWNS_EXPLORATION.md`
(byte layout not actually known yet, anywhere). Each item below is
independently implementable — do them in any order, or split across
sessions, without the cross-item coupling `PHYS_TODO.md`'s single feature
had.

**Convention note, carried over from `PHYS_TODO.md`/`ANIM_TODO.md`**: once
an item below is implemented and tested, remove it from this file outright
(don't mark it `[Done]` and leave it) — fold the permanent record into
`M2_COMPLETENESS.md`/`DESIGN.md`/`README.md` as usual, same as every prior
TODO file's lifecycle (see `CLAUDE.md`'s Resume log for the pattern). When
every item is gone, delete this file the same way `PHYS_TODO.md`/
`ANIM_TODO.md`/`MULTIROOT_SKELETON_TODO.md`/`CORPUS_TODO.md` were.

---

## Priority order (bottom-line-up-front, per this project's own convention)

1. **Item 4 — `PCOL`.** **No longer blocked** — the "0/130,576" result below
   was a real bug in `tools/find_m2_unknown_chunks.py` (a `bytes`-vs-`str`
   dict-key mismatch that made its membership check always `False`), not a
   real absence. Corrected count: **2,354/130,576 real files** carry a real
   `PCOL` chunk (see `WIKI_FINDINGS.md` §10 for the full corrected writeup,
   cross-checked independently via `casc-tool scan-chunks` against a live
   CASC install). Ready to implement against real data now — see the
   "If real data is found" section below, which still applies as written.
2. **Item 9 — `EXP2`/`PFDC` real-data regression tests.** Both are already
   implemented and, as of this session, verified by hand against real
   files pulled from CASC — but that verification only lives in
   `WIKI_FINDINGS.md` §13's prose, not in `./build/husk-tests`/`ctest`.
   Three real fixtures are already sitting in `test_data/verification/`,
   ready to wire in — small, mechanical, no format questions left open.
3. **Item 10 — `BLP2`-masquerading-as-`.m2` negative regression test.**
   Smallest item here: lock in husk's already-correct "throw, don't
   misread" behavior on real non-M2 content with a permanent test, using
   the third fixture already sitting in `test_data/verification/`.

(Items 1 (`M2Sequence`'s missing fields, including `aliasNext` chain
resolution), 2 (`PFDC`), 3 (`EXP2`), 5 (`Texture.type` export), 6
(Attachments/Events/Lights as real glTF nodes), 7 (animated material
tint/fade extras dump), and 8 (`DETL`) are all implemented, tested, and
documented; their sections have been removed from this file. See
`M2_COMPLETENESS.md`, `DESIGN.md`'s Key design decisions, `WIKI_FINDINGS.md`
§12/§11, `README.md`'s Materials paragraph, and `src/cmd_dump.cpp`'s
`dumpPfdc`/`dumpExp2`/`dumpDetl` for the permanent record.)

---

## Item 4: `PCOL` — player-housing collision (War Within 11.1.7+)

### Current state

Held in `kFallback` because the wiki flags its own struct "Preliminary
structure as per Zee's research" — but a full byte-accountable struct
*is* given (counts + offsets for vertex positions, face normals, indices,
flags, each a self-describing region the same way `PLYT`'s polytope shapes
already are in `.phys`).

### Blocker (resolved — was a false negative, not real absence)

A top-level-chunk-tag scan across the full real corpus (`/media/luna/data/
wow_export`, all 130,576 `.m2` files, `tools/find_m2_unknown_chunks.py`)
originally reported **zero** real files carrying a `PCOL` chunk. That result
was wrong: the script's `hits` dict is keyed by `str`, but the per-chunk
check compared the raw `bytes` tag against it (`if tag in hits`) — `bytes`
never equals `str` in Python 3, so the check could never fire, for any tag,
in any file. Fixed (compare against `TARGET_TAGS` directly instead); rerunning
against the same corpus now finds **2,354 real files** with a genuine `PCOL`
chunk (sizes observed: 2,016–12,608+ bytes, varying per file — consistent
with the wiki's self-describing count+offset struct, not a fixed-size
record). Independently reconfirmed via `casc-tool scan-chunks` against a live
CASC install (2,333/130,576 — the ~1% gap matches this corpus's own known
334-file extraction-completeness gap, not a disagreement). Full writeup:
`WIKI_FINDINGS.md` §10.

### If real data is found

Follow the same "self-describing offset region, full byte-accounting
cross-check" discipline `PLYT`'s implementation already used (compute
expected total size from the struct's own count fields, assert it equals
the chunk's real size before trusting the offsets) — `PCOL`'s own struct
has exactly this shape (`vertexPosCount`/`vertexPosOffset` pairs, etc.),
and the wiki explicitly warns "there can be extra bytes between the data,
use the offsets" — i.e. don't assume the regions are contiguous, this is
not a `PLYT`-style single dense blob.

### Docs to update

`M2_COMPLETENESS.md`'s Collision & physics section — new row, likely
`n/a` glTF-ceiling initially (extras, no native slot, same class as
`.phys`) unless a real file surfaces and a translation to husk's existing
`collision` extras-tagged mesh makes sense.

---

## Item 9: `EXP2`/`PFDC` real-data regression tests

### Current state

Both chunks are fully implemented (`dumpExp2`/`dumpPfdc`, `src/cmd_dump.cpp`;
`ExtendedParticle`, `src/m2.hpp`/`m2.cpp`) and, per `WIKI_FINDINGS.md` §13,
now hand-verified against two real files pulled directly from a live CASC
install — but that verification is a one-off `casc-tool extract` +
`husk dump-chunks` + eyeball session, not a repeatable test. Both source
comments (`src/m2.hpp`'s `ExtendedParticle`, `src/cmd_dump.cpp`'s
`physPayloadRealLength`) and `M2_COMPLETENESS.md` were corrected to say
"verified" rather than "unverified, zero real files" this session — this
item is what makes that claim actually checked by `ctest`, not just prose.

Three real fixtures are already sitting in `test_data/verification/`
(gitignored, same "real, personally-owned extraction, never committed"
convention as every other `test_data/` fixture):

- `exp2_126382.m2` — `EXP2` only, 9 particle emitters, all default
  `zSource`/`colorMult`/`alphaMult`, empty `alphaCutoff` curves.
- `pfdc_1003471.m2` — `EXP2` **and** `PFDC` together, 4 particle emitters;
  `EXP2` has a real, non-trivial 3-keyframe `alphaCutoff` curve
  (life_fraction 0 → 0.500015 → 1, values all 0 on this particular file —
  re-check the real decoded values by hand before hardcoding an assertion,
  don't trust this TODO's own summary); `PFDC` decodes to a real
  version-6/`phyt`-3 physics body record.

### Implementation plan

1. Add `HUSK_TEST_EXP2_M2`/`HUSK_TEST_PFDC_M2` resolution functions to
   `tests/test_data_paths.hpp` (`fixtures::kExp2VerificationM2` =
   `"verification/exp2_126382.m2"`, `fixtures::kPfdcVerificationM2` =
   `"verification/pfdc_1003471.m2"`), same `resolve()`-based pattern every
   other real fixture already uses — no new plumbing needed, `resolve()`
   already handles the auto/override/skip cases.
2. New `doctest::skip()`-gated `TEST_CASE`s (`tests/test_dump.cpp`,
   matching where the other chunk-JSON-shape tests already live) running
   `dump-chunks` against both fixtures and asserting exact, hand-checked
   values from the real `EXP2`/`PFDC` JSON output — not just "the key
   exists" or "no exception," the same "exact assertions, not loose
   bounds" discipline this project's own `CLAUDE.md` history keeps
   re-stating (see e.g. the `ANIM_TODO.md` session's
   `anim_69_0`/`anim_69_1` exact-name fix). Re-derive the exact expected
   numbers from a fresh `husk dump-chunks` run against the committed
   fixture at test-writing time — don't copy this TODO's own summary
   numbers verbatim, they're for orientation, not for pasting into an
   assertion.
3. `tests/test_main.cpp`'s startup banner gains the two new fixture lines,
   matching every other real-fixture banner entry.

### Docs to update

`M2_COMPLETENESS.md`'s `EXP2`/`PFDC` rows and `WIKI_FINDINGS.md` §13 both
gain a "now covered by `tests/test_dump.cpp`" pointer once this lands —
same pattern every other §-to-test cross-reference in `WIKI_FINDINGS.md`'s
"Where these live in husk" table already uses.

---

## Item 10: `BLP2`-masquerading-as-`.m2` negative regression test

### Current state

`husk` already does the right thing here — handed a real file whose
content is a genuine BLP2 texture rather than an M2 (`test_data/
verification/blp2_7507381.m2`, the real FileDataID-7507381 listfile
mismatch `WIKI_FINDINGS.md` §13 documents), it throws a clean
`ParseError` rather than silently misreading garbage. There's just no
test locking that behavior in — a future refactor of the chunk walker or
the top-level tag-sniffing logic could regress this silently.

### Implementation plan

1. Add `HUSK_TEST_BLP2_ANOMALY_M2` / `fixtures::kBlp2AnomalyM2` =
   `"verification/blp2_7507381.m2"` to `tests/test_data_paths.hpp`, same
   pattern as Item 9.
2. New `doctest::skip()`-gated `TEST_CASE` (`tests/test_cli.cpp`, next to
   the other real-file-throws-cleanly cases) asserting `husk info`/
   `husk export`/`husk dump-chunks` against this fixture all fail with a
   real, non-crashing error — check the *shape* of the failure (throws,
   process exits non-zero, error text mentions the malformed tag/chunk),
   not necessarily the exact wording, since the exact message isn't a
   contract this project has committed to elsewhere.

### Docs to update

None required beyond what §13 already has — this item is pure test
coverage for an already-correct, already-documented behavior.
