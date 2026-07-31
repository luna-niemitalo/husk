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

1. **Item 4 — `PCOL`.** Blocked on finding a real file with this chunk at
   all (War Within 11.1.7+, player-housing furniture) — a corpus search
   found zero real files carrying it (0/130,576, see the item's own section
   below); it just waits for newer extraction data. The only item left in
   this file.

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

### Blocker

**Real test data, not structural uncertainty.** This chunk only appears on
11.1.7+ player-housing furniture models — check whether the corpus at
`/media/luna/data/wow_export` has any real coverage of that expansion
range at all before investing implementation time. If it doesn't, this
item just waits for newer extraction data; don't invent synthetic-only
coverage for a "preliminary" wiki struct with zero real-byte
cross-checking, same caution this project already applies elsewhere
(`kMinVerifiedParticleVersion`'s whole reason for existing).

**Checked**: a top-level-chunk-tag scan across the full real corpus
(`/media/luna/data/wow_export`, all 130,576 `.m2` files, same fast scan
`tools/find_m2_unknown_chunks.py` already established) found **zero** real
files carrying a `PCOL` chunk. This item stays parked exactly as scoped
above — waiting for newer extraction data, not implemented against a
"preliminary" wiki struct with no real bytes to ground it.

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
