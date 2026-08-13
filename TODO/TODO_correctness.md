# TODO: correctness &amp; usability gaps

**Status: an open punch list, not a historical record.** Fixed items get
removed outright rather than kept as `[Fixed]` noise — git history is where
the record of what was fixed and when lives, not a checked-in file.

## `-o` requires the exact right shape of path already existing — a real usability hurdle, caught live

Prompted directly (2026-08-13) with a full, real terminal trace of trying
to export a single file to `./example_exports/` (the project's own
example-output convention) — four attempts before one worked:

```
husk export ... -o ./example_exports        # "Is a directory"
husk export ... -o ./example_exports/        # "Is a directory" (trailing slash doesn't help)
husk export ... -o ./example_exports/creature/tripod2/tripod2.glb   # "No such file or directory"
mkdir -p ./example_exports/creature/tripod2
husk export ... -o ./example_exports/creature/tripod2/tripod2.glb   # finally works
```

Every one of those four commands ran the **full parse/export pipeline
first** (real work, several seconds, a wall of notes/warnings) before
failing on the trivial part — a real "did all that work just to be told
about a path problem at the very end" experience, not a fast-fail.

**The real design principle this should follow, stated directly**: husk's
default behavior should be *"sinne päin ja silmät kiinni"* — roughly,
"aim in the right direction and go, eyes closed" — get something reasonable
done from minimal, approximate input, guessing what the user almost
certainly means, rather than demanding the exact right shape up front.
Precision is still available for anyone who wants it (every flag `husk
export` already has stays exactly as exact as it is today) — the point is
that guessing a sensible default should be the *zero-effort* path, not
something that requires already knowing the tool's exact expectations.
`-o` currently does the opposite: it's maximally strict (must be an
existing directory's child, parent directories must already exist) with no
guessing at all.

Concretely, two independent real gaps in the trace above:

1. **`-o <existing directory>` should default to `<directory>/<model-basename>.glb`**,
   not fail with "Is a directory" — the model's own filename (already known,
   already parsed) is the obvious guess for what the user wants the output
   named, the same way `cp foo.txt somedir/` infers the destination
   filename from the source. Only fail outright if that inferred path
   itself is somehow unwritable.
2. **Missing parent directories for an explicit output path should be
   created (`mkdir -p`-style), not treated as a hard failure** — a `-o`
   path is where the user *wants* the file to end up, not an assertion that
   the directory structure already exists. Every other tool in this
   ecosystem that takes a real destination path (git, cp -p roughly,
   most exporters) either creates the parent or offers to.

Both fixes should land together — item 1 without item 2 still fails the
`-o ./example_exports/creature/tripod2/` case (no existing directory to
default into); item 2 without item 1 still fails the plain `-o
./example_exports` case (a real directory, but nothing tells husk what to
name the file inside it). Neither is implemented yet — this is the
finding, not the fix. Also worth doing regardless of the specific fix
chosen: fail fast on an obviously-bad `-o` (e.g. resolve/validate it
*before* running the export pipeline), so a path mistake doesn't cost a
multi-second full re-parse to discover.

**Note from an earlier conversation, relevant to item 3 below (`.bone`
slot selection) now that real local DB2 access is confirmed in scope
(`CHAR_TEXTURE_COMPOSITING_TODO.md`'s own Background) — scope clarified
directly by Luna (2026-08-08, two passes)**: a DB2→SQLite exporter is a
separate side project, not part of the real pipeline — "the real pipeline
is the same as with modern blp's — read the file, transform in memory,
write to gltf." Whatever resolves this item's `.bone`-slot-selection
lookup (a `ChrCustomizationBoneSet`-shaped table, if it exists locally)
reads the real WDC5 `.db2` bytes directly at runtime, no SQLite
round-trip. The SQLite side project itself is bigger than a debugging
convenience, though: a real relational schema (mapping/join tables for
DB2 tables' real foreign-key relationships, not one flat table per file),
serving two purposes — a correctness cross-check for the WDC5 parser, and
a general-purpose local data source for other consumers of this project's
work beyond `husk export` itself, expected to matter a lot once WMO/ADT
world-data implementation starts. Full detail, don't duplicate further:
`CHAR_TEXTURE_COMPOSITING_TODO.md`'s own note.

Former item 1 (particles, `M2Particle`) is exactly that case — a real
weapon-model corpus (Luna's own extraction, `test_data/item/
objectcomponents/weapon/`) finally made the "needs real-file investigation"
blocker addressable: `M2Particle` is now fully parsed (every static field,
every animation curve — FBlock-based color/alpha/scale/UV curves and
`M2Track<float>` simulation parameters alike) for version ≥
`kMinVerifiedParticleVersion` (272, Cataclysm — the shape genuinely changed
there; older versions are real but unverified, not attempted), split
between a minimal `.glb` extras placement anchor and `husk dump-chunks`'s
full JSON output (see `../DESIGN.md`'s Key design decisions, `../WIKI_FINDINGS.md`
for the real-data cross-check). `M2Ribbon`'s own remaining tracks got
finished in the same pass. Removed outright rather than kept as a done-item
note, and every remaining item below renumbered accordingly (1-4, was
2-5) — a deliberate, fully cross-checked exception to "don't renumber, it
touches live code strings," since leaving a numbering gap forever would be
worse than a one-time careful rename.

Former item 3 (multi-texture-layer arithmetic) is now resolved the same
way: a full real-data scan (Luna's own extraction, ~287k `.skin` files and
~130k `.m2` files) found and confirmed both a real `textureCount > 1` batch
and a real nonzero `textureCoordCombos` table, hand-verified byte-for-byte
against an independent parse (see `../WIKI_FINDINGS/M2/skin.md`) and now backed by
permanent real-data regression tests (`tests/test_integration.cpp`'s
`checkMultiTextureLayerArithmetic`, gated on `test_data/world/
replaceabletextureprops/guild/pennant_guild_alliance_a_01.m2` and
`test_data/world/expansion05/doodads/ironhorde/
6ih_ironhorde_siegeweapon03.m2`). Removed outright and the remaining item
renumbered accordingly (1-3, was 1-4) — same one-time exception as above.

Former item 4 (texture-transform pivot-correction math, constant case) is
now resolved: `gltf_mesh.cpp`'s `textureTransformToKhr` derives a real
`KHR_texture_transform` (offset/rotation/scale) from M2's texture-center-
pivoted rotation whenever a batch's `M2TextureTransform` is genuinely
constant (not just "every per-sequence value happens to be identity" — a
real `brewfestmount.m2` counterexample this session found: its rotation
value is constant, but its translation/scaling tracks are still
per-sequence-structured, so it correctly stays extras-only) and the
rotation is planar (Z-axis only, the only case wowdev.wiki's own pivot note
describes). Verified three independent ways against real
`bloodknightcharger.m2` data: by hand, against 20,000 randomized trials of
the real client's own translate-rotate-translate matrix composition
(`reference/wow.export`'s `M2RendererGL.js`), and via headless Blender's
own glTF importer producing an exactly-matching Mapping node. Removed
outright per this file's own convention; `../M2_COMPLETENESS.md`'s "Texture
transform (constant case)" row updated to `native — 100%` to match.

Former item 2 (five lookup-table arrays parsed, never referenced) is now
resolved: `husk info` (`cmd_info.cpp`) dereferences all five
(`sequenceLookup`/`boneLookup`/`textureLookup`/`attachmentLookup`/
`cameraLookup`) via the existing `m2::parseUint16Array`, resolving names
through the already-transcribed `keyBoneName`/`textureTypeName`/
`attachmentTypeName` tables where the id has one and skipping 0xFFFF
("-1") sentinels; `sequenceLookup` is printed as its real hash-bucket
shape (wowdev.wiki M2#Animation_Lookup: `bucket = anim_id % count`,
quadratic probing), not a direct id-index map, since that's what the wire
format actually is. Verified against real `wolf.m2`/`bloodelffemale_hd.m2`
data (key-bone entries resolve to the expected Head/Jaw/Root joints,
texture-type entries match each model's own hardcoded slots) plus a new
synthetic regression test. Diagnostic-only, by design, same as
`boneCombos`/`textureCombos` and every other indirection table here —
`husk export` already substitutes full per-vertex global joint indices
and real embedded textures for the batching schemes these tables exist to
drive, so there's no render-pipeline consumer for them to feed. Removed
outright per this file's own convention; `../M2_COMPLETENESS.md`'s lookup-
tables row updated to match. Remaining item renumbered accordingly (1-2,
was 1-3) — same one-time exception as the removals above.

Item 1 (cameras) is low-priority by design, not by oversight; item 2's LOD
hypothesis has since been ruled out by real data, and the extras-export
half is now implemented (`husk export --bones-dir`) — what's left needs a
client-side DB2 lookup husk doesn't have access to, not more file-reading
or export work. Former item 4 (`M2Sequence.aliasNext`
resolution) is resolved outright, not just further investigated: a
`M2_UNKNOWNS_EXPLORATION.md` investigation pass found the field is a plain
local index into the same file's own `sequences` array (at the real,
`M2Bounds`-corrected byte offset 0x3E, not the wiki's literal
pre-correction 0x22) — see `../WIKI_FINDINGS/M2.md` for the full evidence,
including what the earlier "doesn't resolve" finding actually got wrong
(reading the field at the wrong offset). Removed outright per this file's
own convention rather than left as a resolved-but-lingering item; the
follow-up (using this to produce real animation clips for currently-
skipped alias sequences) was itself implemented and removed from
`M2_GAPS_TODO.md` (its former Item 1) — see `../DESIGN.md`'s Key design
decisions. Formerly-tracked items that got fixed and folded back into
`../README.md`/`../DESIGN.md` (shell completion, `.phys`/`PFID` surfacing,
`M2Ribbon`, `M2Particle`, multi-texture-layer arithmetic, and everything
`FINDINGS.md` used to track before it was retired) were removed from this
file entirely.

---

## Read-pipeline correctness

### 1. Cameras (`M2Camera`) — low priority, explicitly deprioritized

**Explored per request: why would a custom-engine emulator need these at
all?** `M2Camera` records are WoW's own *baked, model-relative* cinematic
camera paths — used for things like the character-select rotating camera,
some cutscenes, and login-screen framing. They are fixed viewpoints
authored by Blizzard for *their* UI/cinematic contexts, not something a
model needs in order to render correctly from an arbitrary camera a custom
engine already owns. Unlike billboarding, nothing about normal model
rendering depends on `M2Camera` data — a custom renderer supplies its own
camera unconditionally.

**Verdict: low priority.** Worth only if the goal ever expands to
literally reproducing WoW's specific character-select/cinematic screens,
not general model rendering. Currently count/offset-only in `husk info`;
leave as-is.

---

### 2. `.bone` correction matrices — which slot applies is unresolvable, extras export is done

`husk dump-chunks <file.bone>` surfaces the raw `(bone_index, matrix)`
pairs (see `../README.md`'s `.bone` section); nothing about which of a
model's several `.bone` files (per its `BFID` array) applies to which
context is documented anywhere, on the wiki or otherwise.

The LOD/render-distance hypothesis is ruled out by real data
(`../WIKI_FINDINGS/BONE.md`'s follow-up). All 20 `.bone` files
`bloodelffemale_hd.skel`'s `BFID` lists don't fit `LDV1`'s `lod_count: 7`
at all (20 vs. 7, no clean relationship), collapse into only 5 distinct
bone-index sets with heavy exact-duplication (one 33-bone set repeats
verbatim across 10 of the 20 files), and where a bone is corrected across
multiple files the correction is a pure magnitude scale along one of just
two fixed directions — the signature of a small number of shape variants
reused across many selectable slots (several sharing texture/color-only
choices), not a per-LOD detail-reduction ladder. The corrected bones
themselves cluster tightly around the model's Head/Jaw bones (parent chain
confirmed via `.skel`'s own `SKB1` records), nowhere near hand/grip or
armor-fitting bones — ruling out a weapon-type or armor-type selector too.

**Extras export: done.** `husk export --bones-dir <dir>` resolves every
FileDataID the model's/`.skel`'s `BFID` array declares to a real
`<dir>/<FileDataID>.bone` file (same local-directory, never-CASC
convention as `--textures`/`--skin-dir`/`--anim`), parses it via the
existing `husk::bone::parse`, and attaches every resolved slot's
`(bone_index, matrix)` pairs as `bone_correction_sets` on the exported
glTF skin's `extras` — inert, never applied to the bind pose or any
animation (see `../DESIGN.md`'s "Key design decisions" for why, `../README.md`'s
Usage section for the flag itself).

**What's still open, and why it isn't closed yet -- correction: this is
not an "out of scope" wall.** Which in-game customization choice picks
`BFID[7]` vs. `BFID[13]` most plausibly lives in a `ChrCustomizationBoneSet`-
shaped DB2 table (going from memory -- not confirmed against a real DB2
dump). **Locally-extracted `.db2` files are real, in-scope input** per
`../DESIGN.md`'s Non-goals section (the *only* hard boundary is never
talking to *live* CASC/DB2 or depending on the CASC tool itself -- a
`.db2` file already sitting on disk is the same tier as any other sidecar
`--textures`/`--skin-dir`/`--anim` already read) -- `husk db2-export`/
`--db2-dir`/`--dbd-dir` already exist and already resolve *other* DB2
tables successfully (`CHAR_TEXTURE_COMPOSITING_TODO.md`). An earlier
version of this paragraph said husk "has no access to" this data and
"never will" -- that was wrong, not a scope decision, and got copied
around long enough to become a standing misconception; corrected here.

**Update, same correction pass: the table is not missing.**
`/media/luna/data/wow_export/dbfilesclient/chrcustomizationboneset.db2`
exists locally and is populated -- `husk db2-info` confirms 560 real rows,
2 fields (`bitpacked_signed`, 24 bits each, e.g. row 0: `[1056459,
1000764]`), `header.flags & 0x04` ("has non-inline IDs"), id range
`[24, 742]`. So this genuinely is a real, present, in-scope local file --
the actual remaining work is (a) resolving real column names for it via
`--dbd-dir` (unconfirmed whether WoWDBDefs has a matching `.dbd` layout for
this table -- `husk db2-export chrcustomizationboneset.db2 out.sqlite
--dbd-dir reference/WoWDBDefs` answers this directly) and (b) figuring out
which *other* table/column actually links a specific `BFID[]` slot index to
one of this table's rows for a given customization choice -- this table
alone gives (something, boneset-id) pairs, not "which slot for which
choice." That second join is the real unresolved question, not DB2 access
itself. Next real step: run the `--dbd-dir` resolution above, then search
WoWDBDefs' own `.dbd` files for whatever table has a foreign key into
`ChrCustomizationBoneSet` (a `ChrCustomizationElement`/`ChrCustomizationChoice`-
shaped table is the likely candidate, both of which also exist locally
per `dbfilesclient/`'s own listing) to find the real join path.
