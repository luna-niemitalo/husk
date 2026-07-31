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

(Items 1 (`M2Sequence`'s missing fields, including `aliasNext` chain
resolution), 2 (`PFDC`), 3 (`EXP2`), 5 (`Texture.type` export), 6
(Attachments/Events/Lights as real glTF nodes), 7 (animated material
tint/fade extras dump), 8 (`DETL`), 9 (`EXP2`/`PFDC` real-data regression
tests), and 10 (`BLP2`-anomaly negative regression test) are all
implemented, tested, and documented; their sections have been removed from
this file. See `M2_COMPLETENESS.md`, `DESIGN.md`'s Key design decisions,
`WIKI_FINDINGS.md` §12/§11/§13, `README.md`'s Materials paragraph, and
`src/cmd_dump.cpp`'s `dumpPfdc`/`dumpExp2`/`dumpDetl` for the permanent
record. Items 9/10's tests live in `tests/test_dump.cpp` (real
`EXP2`-only and `EXP2`+`PFDC` fixtures) and `tests/test_integration.cpp`
(the `BLP2`-anomaly throws-cleanly cases across `info`/`export`/
`dump-chunks`).)

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
