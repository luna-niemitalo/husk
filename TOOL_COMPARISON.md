# husk vs. wow.export — static source comparison

Requested directly: "find a way to verify and measure actual coverage and
correctness of this project, and compare it against other tools on the
market for the same solution... key goal being finding cases where this
fails, and or is not behaving as expected."

Two separate questions, two separate answers:

1. **husk's own correctness/coverage, measured against real data** — already
   covered by existing infrastructure (`tools/corpus_test.py`/
   `corpus_checks.py`, `tests/test_conformance.cpp`), current as of the same
   day this doc was written. Not re-run here — see "husk's own scorecard"
   below for the numbers and where to find fresher ones.
2. **Comparison against a real competing tool** — genuinely new. Scoped
   directly by Luna: static source comparison only (what wow.export claims,
   what it actually implements, vs. what husk implements) — a live
   side-by-side export-and-diff run against real files is explicitly
   deferred, since wow.export is "massively pain in the ass to compile /
   get to function, and it crashes frequently" on Luna's own account, marked
   as a future gold-standard validation step rather than attempted here.
   This doc is the answer to that second question.

## Target and methodology

[wow.export](https://github.com/Kruithne/wow.export) (Kruithne/wow.export)
is the real, actively-maintained, community-standard toolkit for this
problem — an Electron app that browses live CASC storage and exports M2/WMO/
ADT/M3/textures to glTF/OBJ/etc. It's the same tool `WIKI_FINDINGS.md`
already cites once (the multi-root-skeleton investigation's "prefix bones"
precedent). Two other GitHub forks exist (`Clewr130841/wow.export-new`,
`Marlamin/wow.export`) but weren't examined — Kruithne's is the canonical
upstream everything else forks from.

Cloned shallow (`--depth 1`) into `reference/wow.export/` (gitignored, not
part of husk's own repo) for direct `grep`/`Read` access rather than
web-fetched summaries — commit `c2fd7bd` (2026-06-22), package version
0.2.19. Everything below is a direct read of that checkout, not a
paraphrase of documentation or a webpage summary, except where explicitly
marked "unverified."

**What this methodology can and can't tell you**: reading source shows what
the tool *attempts* — whether a code path exists, what fields it reads. It
does not confirm the output is correct, and it can't catch a silent
wrong-value bug in either tool (that needs the deferred live-diff run,
below). Treat every "wow.export does X" claim here as "wow.export's source
contains code that attempts X," not "wow.export's X output is correct."

## Feature-by-feature comparison

Same feature groupings as `M2_COMPLETENESS.md`, one column added. `—` means
wow.export's source has no code path for this at all (not even
count-only/skipped-with-a-comment); "skipped" means the field is walked
past deliberately, not silently misread.

### Core geometry & skeleton

| Feature | husk | wow.export | Evidence |
|---|---|---|---|
| Mesh geometry, normals, UVs | native | native | `M2Loader.js` `parseChunk_MD21_vertices` |
| Vertex skinning (weights/indices) | native | native | same |
| Skeleton / bone hierarchy | native (inline or `.skel`) | native (inline or `.skel`, `SKELLoader.js`) | parity |
| Multi-root bone forest (35% of real corpus) | synthesized non-joint glTF parent node, real fidelity-preserving fix (see `DESIGN.md`) | unverified — no equivalent code found, but not specifically searched for | open |
| LOD tiers | native, `--lod all` exports every tier | present (`viewCount`/skin selection read) but depth not compared | open |
| Geometry-less models (0 vertices, pure VFX — 3,807/130k real files) | exports skeleton + emitter anchors, no crash (fixed this project's own bug, see `DESIGN.md`'s "A genuinely geometry-less model" note) | unverified | open |

### Animation

| Feature | husk | wow.export | Evidence |
|---|---|---|---|
| Inline + `.skel`-sourced sequences | native | native | parity |
| External `.anim` (`AFM2`) | native | native, `ANIMLoader.js` | parity |
| External `.anim` (`AFSB`, `.skel`-linked) | native — cracked this project's own reverse-engineering (undocumented on wowdev.wiki), `WIKI_FINDINGS.md` §2 | unverified whether wow.export's `.anim` handling covers this specific undocumented shape | open — worth checking directly if this thread continues |
| Global sequences | native | read (`globalLoops` array parsed) but track-linkage depth not compared | open |
| Alias sequences (`flags & 0x40`, `aliasNext`) | native, chain-walked, `0x20`-wins-priority edge case handled (`WIKI_FINDINGS.md` §12) | **also implemented** — `M2Loader.js:95-97,157-159` walks the identical `while ((animation.flags & 0x40) === 0x40) animation = this.animations[animation.aliasNext];` chain | genuine parity, not a husk-only feature — correct earlier assumption otherwise |
| `M2Sequence` metadata (movespeed/blend times/bounds) | `sequence_metadata` glTF extras | parsed into the `animations[]` record (`id`/`variationIndex`/`duration`/...) but not confirmed exported anywhere | open |
| Animated material tint/fade (`M2Color`/`M2TextureWeight`) | `tint_animation`/`fade_animation` material extras | `parseChunk_MD21_colors`/`textureWeights` parsed; export-side use not confirmed | open |
| M2 header `version` field used to gate record-stride differences | yes — `kMinVerifiedRecordStrideVersion`/`kMinVerifiedParticleVersion`, loud warnings below the verified floor | **`version` is read once and never branched on anywhere in `M2Loader.js`** (`grep -n version` → one assignment, no comparisons) | real, confirmed difference — see caveat below |

**Version-gating caveat**: wow.export splits pre-Legion (flat `MD20`) vs.
Legion+ (chunked `MD21`) into two entirely separate loader files
(`M2LegacyLoader.js` vs. `M2Loader.js`), the same split husk makes — so
"zero version branching inside `M2Loader.js`" isn't necessarily a bug, it
may just mean nothing wow.export parses in the chunked-era loader has a
stride that changed *within* that era for the fields it reads. husk's own
verified floors exist because specific fields (`M2Particle`'s Cata+ layout
being the clearest case) do change shape within the modern era. Whether
wow.export's parsed field set includes anything with the same problem
wasn't checked — flagged as open, not claimed as a wow.export bug.

### Materials & textures

| Feature | husk | wow.export | Evidence |
|---|---|---|---|
| Base material / texture references | native | native | parity |
| Multi-texture-layer (`textureCount > 1`) | `extras`-only — no core-glTF slot for WoW's fixed-function combiner math | wow.export **composites layers itself** at export time (canvas-based texture baking, per earlier doc research — not independently re-verified this session) rather than exposing them separately | **structural difference, not a gap either way** — husk exposes raw per-layer data for a downstream tool to blend correctly; wow.export bakes its own blend into one texture. Neither is strictly more correct: baking requires wow.export's own blend-mode math to exactly match the client's, husk's approach requires the downstream consumer to do the blending itself |
| Hardcoded/replaceable texture slot (`type != 0`) — which *real* texture fills it | placement geometry resolvable locally (`--db2-dir/--dbd-dir/--char-layout-id`), but *picking*/compositing the actual texture per slot isn't yet — blocked on `ChrCustomizationOption`/`_Choice` being genuinely 0-byte in the local extraction, not on scope (`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 3), `texture_type` extras marks the gap in the meantime | **resolvable** — `src/js/db/caches/DBCharacterCustomization.js`, `DBComponentTextureFileData.js`, `DBItemCharTextures.js`, backed by `src/js/casc/db2.js`/`WDCReader.js` (real WDC/DB2 reader) | wow.export's live CASC access means it never depends on a local extraction having those specific tables populated, unlike husk's own current real blocker above — a genuine capability gap for this case, though not the DB2-scope wall it was once described as (locally-extracted `.db2` files are in scope for husk, see `DESIGN.md`'s Non-goals) |
| Texture transform (UV scroll/rotate/scale) | `extras`-only (animated), `native-possible, unverified` (constant) | parsed (`parseChunk_MD21_textureTransforms`) but export-side application not confirmed | open |

### Collision & physics

| Feature | husk | wow.export | Evidence |
|---|---|---|---|
| Core M2 collision mesh (positions/indices/face normals) | native, one more glTF mesh tagged `{"collision": true}` | native — `parseChunk_MD21_collision` fully parses positions/indices/normals; exported as a **separate `.phys.obj`/`.phys.stl` file**, not merged into the main mesh output (`M2Exporter.js:893,958` — note: this "`.phys`" in wow.export's own output filenames refers to the M2's *inline* collision mesh, unrelated to the `.phys` sidecar file format below — confusingly overlapping naming, confirmed by reading the actual write calls, not assumed) | parity in parse depth; different export shape (separate file vs. tagged node in the same `.glb`) |
| `.phys` sidecar file (`PFID` → external physics/collision body-shape-joint file) | **full** — every body/shape/joint/`PHYV` record, `src/phys.hpp`/`phys.cpp`, verified against 103 real files (`WIKI_FINDINGS.md` §9) | **absent entirely** — no `PHYSLoader.js` exists in `src/js/3D/loaders/` (confirmed via directory listing: `ADTLoader`/`ANIMLoader`/`BONELoader`/`DXTDecoder`/`LoaderGenerics`/`M2Generics`/`M2LegacyLoader`/`M2Loader`/`M3Loader`/`MDXLoader`/`SKELLoader`/`TEXLoader`/`WDTLoader`/`WMOLegacyLoader`/`WMOLoader` — no physics sidecar anywhere in that list) | **husk-only feature, confirmed by absence, not inference** |
| `PFDC` (inline `.phys`-shaped chunk) / `EXP2` / `DETL` / `PCOL` | diagnostic (`dump-chunks` JSON), all verified against real corpus bytes | not searched for by name; given no `.phys` sidecar support exists at all, these Legion+/War-Within-era niche chunks are very unlikely to be handled either, but not directly confirmed absent | open, low-priority to chase further |
| `.bone` correction sidecar (`BFID`) | `bone_correction_sets` extras, `husk export --bones-dir` | `BONELoader.js` exists — wow.export **does** load `.bone` files | parity in parsing; whether/how it applies them at export time not compared |

### Interaction points & effects

| Feature | husk | wow.export | Evidence |
|---|---|---|---|
| Attachments | native glTF child nodes | **parsed** (`parseChunk_MD21_attachments`/`attachmentLookup`) | parity in parse; export-side node representation not compared |
| Events | native glTF child nodes (`event_<identifier>`) | **not parsed at all** — `M2Loader.js:363`, `// this.data.move(8); // events`, a dead comment marking the skip, not even a descriptor read | husk-only |
| Lights | native glTF child nodes (position/joint only) | **not parsed at all** — `M2Loader.js:364` | husk-only |
| Cameras | descriptor (count only), deprioritized by design on both sides apparently | **not parsed at all** — `M2Loader.js:365-366` (camera + camera_lookup_table both skipped) | husk is marginally ahead (count-only vs. nothing) but neither tool really implements this |
| Ribbons (`M2Ribbon`) | full parse (every field + 6 resolved `M2Track` curves), diagnostic JSON + glTF placement-anchor extras | **not parsed at all** — `M2Loader.js:367` | husk-only, confirmed |
| Particles (`M2Particle`) | full parse for version ≥ Cata (every field + FBlock/track curves), diagnostic JSON + glTF placement-anchor extras | **not parsed at all** — `M2Loader.js:368` | husk-only, confirmed |

### Sidecars & lookup tables

| Feature | husk | wow.export | Evidence |
|---|---|---|---|
| `SFID`/`TXID`/`AFID`/`BFID` FileDataID resolution | local-directory convention only, never CASC, by design | **live CASC resolution** — wow.export has an actual CASC connection, so it resolves these directly against the real game archive rather than a pre-populated local directory | structural difference: wow.export's approach is strictly more automatic (no manual extraction step) but depends entirely on CASC being present and working; husk's approach works offline against any directory shape, by design (`DESIGN.md` non-goals) |
| `SKID`/`PFID` resolution | surfaced, not resolved (no CASC), by design | resolvable via CASC | same non-goal boundary as above |

## Scope wow.export covers that husk explicitly doesn't

Confirmed by directory listing, not inferred: `ADTLoader.js`/`ADTExporter.js`
(terrain), `WMOLoader.js`/`WMOLegacyLoader.js`/`WMOExporter.js`/
`WMOLegacyExporter.js` (world objects), `M3Loader.js`/`M3Exporter.js` (the
undocumented M3 format husk's own `DESIGN.md` Non-goals section explicitly
parked as "note it, stay out of scope" after finding 8 real `.m3` files in a
full-storage scan). All three are real, substantial format support wow.export
has and husk was never scoped to build — expected, not a finding.

## Where husk is ahead (confirmed, not inferred)

By direct absence-of-code-path in wow.export's own loader:

- Ribbons, particles, events, lights, cameras — **zero parsing**, not even
  count-only (dead `// this.data.move(8)` comments mark every one).
- `.phys` sidecar (physics/collision bodies/shapes/joints) — no loader file
  exists for it at all.
- `PFDC`/`EXP2`/`DETL`/`PCOL` (niche Legion+/War Within chunks) — very
  likely absent given the above, not directly confirmed.
- Real-corpus verification discipline generally: every husk feature above
  is backed by a documented real-file byte-check (`WIKI_FINDINGS.md`), not
  just "the wiki says so." Nothing in this comparison checked whether
  wow.export's own parsing has been verified the same way — its source
  gives no indication either way.

## Where wow.export is ahead (confirmed, not inferred)

- **Live CASC access**: real `WDCReader`/DB2 caches
  (`DBCharacterCustomization`, `DBComponentTextureFileData`,
  `DBItemCharTextures`, `DBNpcEquipment`, ...), queried directly against a
  live game install. husk's own `.bone`-slot-selection gap
  (`TODO/BONE_CORRECTION_APPLICATION_TODO.md`) and hardcoded-texture-slot
  compositing gap (`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 3) aren't
  blocked on DB2 access as such — locally-extracted `.db2` files are in
  scope and already parsed (`src/db2.hpp`/`chrmodel_db2.hpp`/
  `chrcustomization_db2.hpp`) — they're blocked on specific customization
  tables (`ChrCustomizationOption`/`_Choice`) being genuinely 0-byte in the
  local extraction husk was verified against. wow.export's live CASC
  connection sidesteps that extraction-completeness problem entirely by
  never depending on a pre-populated local file in the first place — a real
  capability gap, but not the "husk would need a CASC/DB2 dependency it
  deliberately rejects" framing this used to have (`DESIGN.md`'s non-goals
  reject *live* CASC/DB2 access specifically, not local `.db2` files). Not a
  bug in husk; a real, named, accepted tradeoff (no live extraction
  dependency) with a real, named cost (depends on someone's local
  extraction being complete).
- WMO/ADT/M3 (out of husk's own declared scope, see above).
- No manual extraction step needed (browses CASC directly) vs. husk's
  "point it at a directory someone already extracted" model.

## Genuinely open items (not confirmed either way this session)

- Multi-root bone forest handling.
- LOD-tier export depth.
- Geometry-less (0-vertex, VFX-only) model handling.
- `AFSB` (`.skel`-linked external animation) — whether wow.export's
  `.anim`/`SKEL` handling covers this undocumented shape at all.
- Whether `M2Sequence` metadata / animated tint-fade / texture-transform
  parsing actually reaches wow.export's export output, vs. being parsed
  and dropped.
- Multi-texture-layer baking correctness (does wow.export's combiner math
  match the client's blend modes exactly?).
- Whether wow.export's zero-version-branching in `M2Loader.js` ever
  actually produces a wrong read for a field whose stride changed within
  the chunked-M2 era (particles are moot — unparsed — but nothing else was
  checked).

None of these were dismissed for lack of importance — they're open because
answering them needs either deeper source reading than this pass did, or
the deferred live-diff run below.

## husk's own scorecard (measured against real data, already current)

Not re-run this session per Luna's own direction (numbers are same-day
fresh). From a full local-corpus sweep, `tools/corpus_test.py`/
`corpus_checks.py` against the full local real corpus:

- **130,575 real `.m2` files, 663 failures (99.49% clean)** — header parse
  cross-check, export well-formedness, `dump-chunks` JSON validity, an
  independent-parse-vs-glTF fidelity check, NaN/Inf sweep, and Khronos
  `gltf_validator` schema validation, all per-file.
- Every failure class already root-caused: bad/mismatched source data
  (`materialIndex`/`textureComboIndex` off-by-one-past-array-end, 21 files),
  extraction-completeness gaps (missing `.skin` siblings for some
  spell-effect/item models), and 37 real `.skin`/`.m2` vertex-count
  mismatches on two known model families — none are husk bugs, all
  documented in `README.md`'s extraction-gap paragraph.
- **3 files with a genuinely new, previously-undocumented failure shape,
  not yet tracked in any TODO file** (this is the concrete "cases where
  husk fails" the task asked to surface, beyond what's already written up):
  - `creature/garrosh2/garrosh2.m2` — bone 62's scale keyframe 9 is
    non-finite (NaN/Inf).
  - `creature/harronirchildmale/harronirchildmale.m2` — bone 1's
    translation keyframe 3 is non-finite.
  - `creature/amanitrollchildfemale/amanitrollchildfemale.m2` — bone 2's
    rotation keyframe 1 timestamp is ~2.2 billion ms *before* keyframe 0's
    — out of order by a margin far too large for the existing
    duplicate-timestamp nudge-repair (see `DESIGN.md`'s "A duplicate
    animation keyframe timestamp" note, which only
    handles equal/near-equal timestamps) to apply.

  These read as real corrupted/garbage keyframe data (NaN, or a timestamp
  that looks like unrelated memory/a misread field) rather than benign
  authored data — but that's a hypothesis, not confirmed against the raw
  bytes. Low priority (3/130,575 files) but genuinely open — worth a look
  if animation robustness work resumes.

## Deferred: live side-by-side export diff against wow.export

Explicitly marked as the real gold-standard validation step, not attempted
this session — Luna's own words: wow.export is "massively pain in the ass
to compile / get to function, and it crashes frequently." Whenever this is
picked up (needs a stable environment for wow.export, real judgment call on
new flake packages — Node/Electron/bun, per `CLAUDE.md`'s package-approval
rule):

1. Get wow.export actually running against Luna's real WoW install (same
   CASC storage `casc-tool`/husk's own test fixtures already draw from).
2. Pick a shared sample of real `.m2` files (the existing curated fixtures
   in `test_data/` are a reasonable starting set — already hand-verified
   against husk's own output).
3. Export each through both tools, diff structurally: vertex/bone/animation
   counts, bounding box, material count — the same shape `test_conformance.cpp`'s
   case-1/case-2/case-3 checks already use internally, just with wow.export's
   output as the second data point instead of Blender's readback.
4. Treat a mismatch as a *lead*, not an automatic husk bug — neither tool is
   ground truth; a mismatch means "go read the real bytes and figure out
   which one (if either) is right," same discipline every real finding in
   `WIKI_FINDINGS.md` already used.
