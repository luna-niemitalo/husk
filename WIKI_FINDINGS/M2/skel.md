# wowdev.wiki findings — `M2/.skel`

Current, correct facts only. Full evidence trail: `../../WIKI_FINDINGS_HISTORY.md`
§3.

---

## `SKB1` bone `M2Track` outer-array indexing — verified — history §3

A `.skel`'s `SKB1` bone `M2Track` outer array uses the **same** positional
convention an inline M2's does: index *i* corresponds to the *i*-th record
in this same `.skel` file's own `SKS1.sequences` array — not the owning M2's
`sequences` (many `.skel`-linked models have zero inline sequences of their
own). The wiki's `SKS1` struct shape itself is already correct and already
matches husk's implementation; only this cross-chunk indexing convention was
undocumented. Confirmed on `bloodelffemale_hd.skel` (396 sequences): every
bone's nonzero-count translation track reports outer-array `count = 396`,
and probing a specific inline-flagged sequence index against all 245 bones'
inner arrays finds plausible per-bone keyframe counts, while an
external-only sequence index reads `(0, 0)` for the same bones, exactly as
expected.

## A `.skel`'s own `AFID` is a separate table from the M2's — verified — history §3

The same `(animId, subAnimId)` pair resolves to a **different** FileDataID
in a `.skel`'s own `AFID` chunk than in the owning M2's own `AFID` — looking
up a `.skel`-sourced sequence's external file in the *M2's* `AFID` table
instead would silently resolve to the wrong file whenever that FileDataID
happens to also exist in both tables.
