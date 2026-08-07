# wowdev.wiki findings — `PHYS` (`.phys` physics/collision sidecar)

Current, correct facts only. Full evidence trail: `../WIKI_FINDINGS_HISTORY.md`
§9. Unlike every other sidecar husk has investigated, `.phys` is not
undocumented — `documentation/wowdev-wiki/md/PHYS.md` already gives byte
offsets for nearly every field; this page verifies/extends an existing page
rather than reverse-engineering one from nothing. Verified against 103 real
files. Fully implemented: `src/phys.hpp`/`phys.cpp`, `husk export --phys`,
`husk dump-chunks <file.phys>`.

---

## `PLYT` stride is 0x50 (80) bytes, not 0x38 — verified — history §9

The wiki's own struct listing is textually complete (`unk_38[6]`, 6 trailing
floats, genuinely is the struct's last field) but easy to under-count as
ending at the field *before* it. Decoding at 0x38 produces a plausible-
looking first entry and garbage for every entry after; at 0x50, all entries
decode cleanly and the variable-length data region that follows consumes
**exactly** the chunk's remaining bytes — confirmed across every one of 55
sampled files carrying a `PLYT` chunk, zero exceptions.

## Chunk tags are byte-reversed on disk — verified — history §9

WMO/ADT convention — the **opposite** of M2's own inline chunks. Confirmed
via hex dump on all 103 files, zero unrecognized tags. `husk::readChunks`/
`findChunk` assume the M2 (non-reversed) convention and cannot be reused
as-is for `.phys` — `src/phys.cpp`'s chunk-tag constants are the reversed
literals already.

## `BODY`/`BDY3`/`BDY4`'s "only one type-0 body" claim is wrong — verified — history §9

78 of 98 sampled files with a body chunk have **more than one** type-0 body
(up to 27 of 44 in one real file). Cross-tabulated against `BDY3`'s own
`unk1`-as-kinematic-weight field: 96% of 1,256 sampled bodies cleanly split
type-0↔`unk1==0`, type-1↔`unk1≠0` — type-0 means "kinematic, bone-driven,"
a real per-body classification, not a single distinguished root.

## Version ↔ chunk-name-variant pairing, `SHOJ` stride, cross-chunk index bounds — verified — history §9

Zero exceptions across all 103 files: v0/1 → `BODY`+`SHAP`+`WELJ`, v3 →
`BDY3`+`SHP2`, v4/5 → `BDY4`+`SHP2`. `SHOJ`'s documented version-2 stride
ambiguity (0x6c vs. 0x74) never actually produces an ambiguous case — every
one of 86 real `SHOJ` chunks divides evenly by exactly one of the two.
Every cross-chunk index reference (body↔shape, shape↔shape-type-chunk,
joint↔body, joint↔joint-type-chunk) resolves in range across all 103 files
and 1,256 body records, zero exceptions.

## Not resolved by this investigation

What several `unk`-tagged fields actually mean (`SHOJ`'s `motorMode`,
`PLYT`'s per-node tree-structure fields) — they decode to sane numbers, but
confirming semantics (not just byte layout) needs simulation-behavior
testing, not attempted.
