# EYES_ON_FINDINGS.md

Findings from a real interactive Blender inspection of a `husk export` output
(post-`TRANSFORM_TRIAGE.md` orientation fix). Mesh completeness and
orientation are both confirmed good. **Fully resolved items are punched out
of this file outright** (git history + `CLAUDE_HISTORY.md` have the
record), not kept as a closed log. Full narrative for both items below,
including everything that's already been fixed: `CLAUDE_HISTORY.md`'s
"(archival, 2026-08-08 through several later sessions)" entry.

## 1. `husk info`/`export` given a non-M2 file (e.g. a `.skin` directly) fails with a confusing byte-garbage-looking error, not a clean "wrong file type"

**Root cause**: `src/m2_primitives.cpp`'s `resolveBlob` treats any file
that doesn't start with `MD20` as a Legion+ chunked M2 and runs
`readChunks` over it unconditionally. A `.skin` file's real `SKIN` header
+ next 4 bytes happen to parse as a syntactically valid chunk tag+size,
so `readChunks` "successfully" consumes a fake chunk before hitting real
vertex/index data and throwing a nonsensical size — a real usability gap
(confusing error), not data corruption (the `.skin` file itself is fine).

**Action, still not implemented**: validate the second 4-byte tag against
`MD21` immediately after a failed `MD20` check, before calling
`readChunks` at all — or have `readChunks` bail out with a distinct error
the first time a chunk's declared size would run off the end of the
buffer, naming the file's actual first-4-bytes magic (e.g. "expected an
M2 file (MD20/MD21), got a file starting with 'SKIN' -- did you mean
--skin instead?").

**Developer note**: also worth making `husk info` able to read a `.skin`
file directly and print its own header/magic, so `info` can be used as an
independent exploration tool without always requiring an `.m2` target.

## 2. `alternate_textures`/ambiguous-default-texture investigation — two threads still open

Full history of what this investigation found and fixed (size/runtime
blowup, category filtering, material dedup, several wrong-default
corrections, the DB2-driven compositing discovery that unblocked the
later full character-texture pipeline): `CLAUDE_HISTORY.md`.

**Still open**:

- Three deterministic (`textureType == 0`) texture slots in
  `bloodelffemale_hd.m2` (real FileDataIDs `3536810`/`4530998`/`5210137`)
  turned out to be eye-glow effect textures genuinely absent from the
  local CASC export (confirmed: zero matches anywhere in the local tree)
  — a real upstream extraction gap, not a husk resolution bug. No fallback
  behavior has been decided for a `textureType == 0` slot with no local
  file at all.
- Whether to surface `blendMode`/`unlit` flags as glTF material extras
  (for every material, not just these three) so a Blender script could
  apply its own emissive/additive shader per-slot — proposed, never
  scoped or implemented.
