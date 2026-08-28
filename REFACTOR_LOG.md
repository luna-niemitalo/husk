# REFACTOR_LOG.md — running log of REFACTOR/ migration work

Each entry: what was done, why, what it deliberately did *not* touch, and how
it was verified. Punch-list items get removed from the `REFACTOR/*.md` files
they came from once done (see those files' own "closed items get removed"
convention) — this log is the narrative git history doesn't give you at a
glance, not a duplicate of the plan.

---

## 2026-08-29 — `CLI_AND_TOOLING.md` §3's second half: five corpus-scan tasks converted to `husk info --json`

**What**: `particle_only_task.py`, `detect_billboards.py`, `expansion_task.py`,
`example_texture_count.py`, and `black_additive_task.py`'s own `husk info`
prose regexes (`TEXTURE_LINE_RE`/`LOOKUP_LINE_RE`/`MATERIAL_RE`/
`PARTICLE_COUNT_RE`) now parse `husk info --json` instead of regexing prose
husk never promised to keep stable. New `corpus_scan_framework.husk_info_json(path,
timeout=15.0)` is the one shared invocation point (subprocess + `json.loads`,
returns `None` on any failure the same way every task's own `analyze()`
already treats a skip) — each task kept its own analysis logic, only the
subprocess-and-parse boilerplate moved to the framework, per the brief's
"a sixth copy of the same subprocess call is the duplication this whole
refactor is about." `example_texture_count.py` (the copy-paste template)
now models the structured-output pattern for new tasks to start from.
`detect_billboards.py` also picked up `corpus_scan_framework.HUSK_BIN`
for free (was hardcoding `/home/luna/dev/husk/build/husk`) and lost a
worker-killing `exit(1)` on a bare subprocess exception, both incidental
to routing it through the shared helper rather than a deliberate second
fix. `expansion_task.py` stopped hand-transcribing `expansionForVersion`'s
table — it now reads the real `expansion` field husk itself computes.

**Why**: `REFACTOR/AUDIT.md`/`CLI_AND_TOOLING.md` §3 named these five (plus
`black_additive_task.py`'s prose half) as the "structured output" tier of
`RESOURCE_CATALOG.md`'s excavation-escape-hatch verdict table — tasks
consuming husk's own understanding of a file, not interrogating raw bytes
behind it, so a hand-rolled second implementation is a bug waiting to
happen (already one, per `AUDIT.md` §1.1's tier-2-silently-dropped
incident on the texture-resolution side).

**What it deliberately did not touch**: `black_additive_task.py`'s texture
*resolution* (`_resolve_texture_path`) and pixel-brightness decode
(`_decode_mean_brightness`, still shelling out to `husk blp-export`) —
needs resolved bytes, which is `AUDIT.md` §1.1/`RESOURCE_CATALOG.md`'s
Catalog work, a separate piece in progress by a peer session; only its
four `husk info` prose regexes converted. `unfillable_texture_task.py`,
`texture_dedup_collision_task.py`, `texture_type_collisions_task.py`, and
`m2_full_validation_task.py` — all need structured *resolution* output,
which doesn't exist yet, out of scope per the brief. `shader_id_task.py`/
`shader_names_task.py` (documented raw-read exceptions), `casc_size_mismatch_task.py`,
`dangling_references_task.py`, and every render driver — untouched, per
the brief. Nothing under `src/`/`tests/` touched.

**`animated_texture_effects_task.py` — found blocked, not converted,
contradicting the brief and `RESOURCE_CATALOG.md`'s own verdict table**:
its verdict said "Structured output. Consumes husk's own track resolution,"
but `husk info --json`'s schema has no `colors`/`textureWeights` arrays at
all, and `texture_transforms` is count/offset only — no per-record
animated-vs-constant field exists anywhere in `husk info` or `dump-chunks`.
husk resolves this internally (`resolveAnimatedColorCurve`/
`resolveAnimatedFixed16Curve`, `src/export_materials.cpp`) but exposes none
of it. This is the same considered-exception shape `shader_id_task.py`
already documents for itself, not a task I could honestly convert without
either fabricating a wrong field or adding new `src/cmd_info_json.cpp`
fields (out of scope — "do not touch anything under `src/`", a peer agent
working there). Left the task's actual logic untouched; added a docstring
paragraph naming the gap explicitly, matching `shader_id_task.py`'s own
precedent, so the next reader can tell a considered exception from an
unconverted leftover. `RESOURCE_CATALOG.md`'s verdict table and `AUDIT.md`
§1.1 both updated to reflect this — see those files' own diffs.

**Verified**: a differential run, not a full corpus scan (the brief's own
gate: pure readers whose logic didn't change, so any output delta is a
conversion bug, not a finding). Built a baseline copy of the pre-change
`corpus_scan_framework.py` + the five task files (`git show HEAD:...`,
symlinked `build/husk` to the real binary) and ran both baseline and
converted versions of every task against three real corpus subdirectories
(`creature` 300 files, `item/objectcomponents` 200, `character` 144 — 644
files, three different corpus areas, comfortably past "a few hundred").
Raw CSV diffs showed row-order differences for `detect_billboards.py`/
`expansion_task.py`; traced to `run_corpus_scan`'s own pre-existing
non-deterministic completion order (`rows.append` as
`ProcessPoolExecutor` futures complete, never sorted before `writerows` —
present in both baseline and converted code paths identically, since
`run_corpus_scan` itself wasn't touched) — sorted diffs on all 5 tasks
across all 3 roots came back byte-identical. Also ran targeted single-file/
larger-limit checks to exercise paths the 644-file sample didn't hit:
`black_additive_task.py` against `creature/deathwingcorruptedjaw.m2` (the
file this task's own docstring is about) and `creature/spectralcat2.m2`
(the one file that passed the additive-only filter in the 300-file sample)
both agreed (0 rows, texture resolution/brightness rejected both, matching
before and after); a wider `--limit 1500` run against `creature/` found
one real match (`firesprite.m2`, 5 additive materials, 12 particle
emitters, resolved to `firespriteglowred.blp`, mean brightness 0.000) with
every field byte-identical between baseline and converted output.
`example_texture_count.py` against `cameras/` (all 33 real camera `.m2`
files have zero M2Texture records) came back byte-identical sorted. One
gap not exercised by any real local file: `expansion_task.py`'s
`"sketchy"`/`"out_of_scope"` tiers — every file checked (creature/
objectcomponents/character/cameras, ~2,000+ files total across all runs)
is Legion+ chunked (`MD21`), consistent with this being a live-patch CASC
extraction with no older-format assets present; the tier logic itself is
a two-line boolean read off husk's own `record_stride_version_verified`
field, low risk, but genuinely untested against real non-chunked data.
`REFACTOR/RESOURCE_CATALOG.md`'s task table and `REFACTOR/AUDIT.md` §1.1
updated to reflect exactly what converted.

---

## 2026-08-28 — `husk::sources::Catalog`: the real texture-tier object, AUDIT.md §1.1's C++ side closed

**What**: `src/sources/catalog.hpp`/`.cpp` — the object `RESOURCE_CATALOG.md`
has been describing since it existed: one place that owns the texture tier
order (literal → listfile → fuzzy same-basename pool) and the mutable pool
state tier 3's claim-and-remove step needs, replacing
`export_materials.cpp`'s inline three-way branch (claimed-and-read /
nothing-claimed-so-scan-for-ambiguity / 2+ candidates) with one
`texture(fdid, textureType, modelContext, preferGlowVariant) ->
Resolved<EncodedTexture>` call per slot. `EncodedTexture` is `{bytes,
encoding, imageName, matchedFilename}` — `encoding` (`TextureEncoding::Png`/
`Blp`) exists per I8/`BUNDLE_FORMAT.md`'s "Texture encoding" even though
every producer today still tags `Png` (every real tier still decodes BLP →
PNG on read, unchanged — the tag is what makes moving that decode to a
writer later a call-site change instead of a type change, not a claim that
the decode already moved). `Resolved<T>` (`resolved.hpp`) gained the
`alternates` field `RESOURCE_CATALOG.md`'s Settled section describes — a new
`Alternate` struct (filename/category/width/height/imagePng), non-empty
meaning genuinely ambiguous, the chosen default included in the list rather
than excluded from it. `Catalog::registerPathOverride(fdid, path)` replaces
`resolveObjectSkinTextureFromKb`'s `listfile.emplace(...)` sideways mutation
(`AUDIT.md` §7, now closed) — the `--knowledge-db` object-skin substitution
still runs as its own pre-step in `cmd_export.cpp` (a genuinely different
question — "which fdid stands in for this slot" vs. "given this fdid, find
bytes" — not folded into `texture()`), but no longer touches the caller's
own `--listfile` map to do it. `Catalog` is constructed once per
`exportOneModel` call (`cmd_export.cpp`), shared across every LOD tier of
that one model's own `buildMaterialsAndPrimitives` calls, not reconstructed
per tier — not yet hoisted further up to span a whole `--from-list` batch
(see "What it deliberately did not touch").

**Why**: `AUDIT.md` §1.1's own framing — "the tiers exist; the object that
owns the tier order does not" — plus a real, load-bearing consequence: tier
3's claim-and-remove pool state and its per-M2-texture-slot memoization
(`fuzzyResolutionByTextureIndex`, guarding against the real
`argustalbukmount.m2` bug where two batches sharing one M2 texture-array
entry could otherwise deplete the shared pool twice) lived as local
variables in `export_materials.cpp`, unreachable by anything else and
un-testable except through a full CLI export. Neither is true anymore.

**What it deliberately did not touch**:
- **Tier 4 (parent-directory same-basename)** — Blender-script-only today,
  a marked gap in `Catalog::texture()`'s own doc comment pointing at
  `RESOURCE_CATALOG.md`. Porting it changes real resolution outcomes for
  real files and needs its own ledger run — out of this pass's scope, per
  the brief.
- **The Python/Blender mirrors** — can't call into `src/sources/` at all;
  `AUDIT.md` §1.1 stays open for exactly this reason. `CLI_AND_TOOLING.md`
  §3's structured-output work is the eventual bridge.
- **Hoisting `Catalog` above one model's own export.** `RESOURCE_CATALOG.md`'s
  "constructed once per export... never per model" is satisfied at the
  single-model granularity (shared across that model's own LOD tiers, which
  it wasn't before — each `buildMaterialsAndPrimitives` call used to rescan
  a fresh, independent pool per tier); sharing one instance across an entire
  `--from-list` batch (a coarser, also-valid reading of that same sentence)
  would need `exportOneModel`'s own signature to accept an externally-owned
  `Catalog&`, a larger integration change than collapsing the tier
  orchestration itself. Nothing in `Catalog`'s own state assumes
  single-model lifetime (model-scoped state is already keyed by
  `modelPath`, not by the object's lifetime) — the object is ready for that
  hoist, it just isn't wired that far up yet.
- **The additionalTextureLayers loop** (`export_materials.cpp`) still calls
  `resolveLiteralTextureBytes`/`resolveListfileTextureBytes` directly, not
  `Catalog::texture()` — it's deliberately literal/listfile-only, never the
  shared fuzzy pool (supplementary per-layer metadata competing for the same
  claim-and-remove pool as a model's primary hardcoded slots would be a new
  behavior, not a refactor).
- **The two remaining `listfileRoot.empty() ? texturesDir : listfileRoot`
  re-derivations** (`cmd_export.cpp`, `export_materials.cpp`'s own function
  signature) stay, alongside `Catalog`'s own constructor doing the identical
  fallback for its internal use — both existing sites serve callers beyond
  `Catalog` (`attachCharTextureLayout`, `resolveObjectSkinTextureFromKb`,
  `exportGearAuxItemModels`, ...) that need a real `listfileRoot` string
  independent of the catalog. `RESOURCE_CATALOG.md`'s "resolved once, not
  re-derived per caller" is true *inside* `Catalog` (and `describe()` names
  the effective root); it is not yet true project-wide, and this pass didn't
  claim otherwise.

**Verified**: full suite green, 738/738 (728 baseline + 10 new
`tests/test_sources_resource_catalog.cpp` cases: a hit per tier, a total
miss with a reason, genuine ambiguity producing `alternates` including the
chosen default, pool depletion across two distinct slots, per-slot
memoization across two batches sharing one M2 texture index, the own-fdid
pool exclusion, `registerPathOverride` resolving without mutating the
caller's map, and `describe()`'s ledger content) — 0 regressions.

Resolution-ledger diff (this stage's actual gate, `REFACTOR/README.md`'s
"every difference is explained and attributed"): built a `husk-before`
binary from the pre-`Catalog` commit and a `husk-after` binary from this
change, then ran real `husk export` against the four required fixtures plus
two more, diffing stdout/stderr and the output `.glb` byte-for-byte:

| Fixture | Real tier-3 activity | `.glb` bytes |
|---|---|---|
| `test_data/character/bloodelf/female/bloodelffemale_hd.m2` | 25 fuzzy/ambiguous matches (default `--textures`) | identical |
| `nightelffemale_hd.m2` (`--textures` at the real corpus dir) | a real 218-candidate ambiguous pool on one slot | identical |
| `creature/wolf/wolf.m2` (single-LOD only — see the correction below) | 2 fuzzy/ambiguous matches | identical |
| `item/objectcomponents/weapon/sword_1h_artifactskywall_d_06.m2` | 13 fuzzy/ambiguous matches | identical |
| `item/objectcomponents/shoulder/lshoulder_robe_d_01.m2` `--knowledge-db` | real KB hit, exercises `registerPathOverride` | identical |

**Zero delta, every fixture, honestly reported** — not manufactured
significance. The four tiers already agreed with each other in the real
C++ implementation before this pass (the drift `AUDIT.md` §1.1 documents is
against the *Python/Blender* mirrors, untouched here); consolidating them
into one object changed where the tier order lives, not any resolution
outcome.

**Correction, from the independent verification pass (same day).** The
original version of this entry claimed the `--lod all` multi-tier case was
exercised and found byte-identical. **It was not, and cannot currently be**:
`husk export test_data/creature/wolf/wolf.m2 --lod all` *fails* — `auto`
resolves SFID entry 0 but not entry 1, since the corpus names LOD skins
`<basename>_lod01.skin` while `auto` looks for `<basename>1.skin` (the
pre-existing gap already tracked as `TODO/CLEANUP_TODO.md`'s `--skin auto` +
`--lod` item). The ledger row compared two *failed* exports, which are indeed
identical to each other and prove nothing. Re-checked against the real corpus
too (`nightelffemale_hd.m2 --lod all`, 7 real LOD tiers on disk): same
failure, same cause. **No fixture in `test_data/` has more than one `.skin`
at all**, so there is currently no way to run this case.

The change is therefore **real and unverified**, and it deserves naming
plainly rather than leaving inside a "not proven exhaustively" hedge. Before
this pass, `buildMaterialsAndPrimitives` built its own `FuzzyTexturePool` per
call — its own comment said "scanned once per skin/LOD" — so each LOD tier
got a *fresh* pool. Now one `Catalog` spans every tier of a model. Reading
the code, the outcome should be *better*, not merely equal: `texture()`
memoizes per `(modelPath, textureSlotIndex)`, and LOD tiers of one model
share both, so every tier now returns one agreed answer, where the old
fresh-pool-per-tier design could hand two tiers different files for the same
slot depending on batch order (claim-and-remove is order-sensitive). That is
a plausible fix, not a regression — but it is reasoning, not measurement, and
this project's gate is measurement.

Verifying it needs a real multi-LOD fixture husk can actually resolve, which
is blocked on the `--skin auto` + `--lod` gap above. Recorded here rather
than closed.

---

## 2026-08-28 — `husk info --json`: CLI_AND_TOOLING.md §3's first half

**What**: `husk info --json` (`src/commands.hpp`'s new `InfoOptions::json`,
`src/cmd_info.cpp`, new `src/cmd_info_json.hpp`/`.cpp`) prints a structured
JSON document with every field the existing prose path prints -- format/
version/name, `global_flags` (raw value, hex, decoded bit names), every
`m2::Array`-backed field as `{"count", "offset"}` plus a same-named
`entries` array wherever the prose path dereferences it (sequences, bones,
textures, materials, attachments/events/lights/cameras, ribbon/particle
emitters, ...), sidecar FileDataIDs (skin/bone/anim/phys/texture),
chunk tags plus which ones are outside husk's known-tag list, and the
bounding/collision geometry. Three fields get particular attention since
real consumers already scrape them out of prose today
(`tools/corpus_scan_tasks/black_additive_task.py`/`particle_only_task.py`):
`vertices.count`, `particle_emitters.count`, and `materials[].blend_mode`.

**Why**: `REFACTOR/AUDIT.md`/`CLI_AND_TOOLING.md` §3 named this directly --
eight corpus-scan tasks regex-scrape `husk info`'s prose with patterns like
`^\s*particle_emitters: (\d+) ` compiled against text husk never promised
to keep stable. A real JSON output gives those tasks something to actually
parse.

**What it deliberately did not touch**: converting
`tools/corpus_scan_tasks/*.py`'s own regexes over to consume this JSON --
explicitly out of scope per the brief, a separate later pass; those eight
tasks still scrape prose today, unchanged. The `husk resolve` verb §3 also
mentions is untouched too. Prose output itself is unchanged, byte for byte
-- verified by diffing `husk info <file>` (no `--json`) against a binary
built from the pre-change commit across 7 real fixtures (character,
creature, weapon, and VFX models spanning EXP2/PCOL/coord-combo chunk
content) plus the error/no-args/`--help` paths; the only difference
anywhere is `--help`'s new `--json` line, which is expected. Internally,
`documentedM2ChunkTags`/`isUndocumentedChunkTag` moved from an anonymous
namespace in `cmd_info.cpp` to `namespace husk::commands` (declared in
`commands.hpp`) so the new JSON path can share the exact same curated
chunk-tag list instead of carrying a second, driftable copy -- a pure
visibility change, logic untouched.

**Verified**: new `tests/test_cli_info_json.cpp` (7 cases) -- flag absence
leaves prose alone (byte-diffed manually as above, plus a regression test
asserting no `{` appears), `--json` emits a single well-formed JSON
document (bracket/string-aware balance check, no JSON library added, same
"don't add a JSON library" scope the brief set), the three consumer fields
resolve correctly against both a synthetic fixture and two real ones
(`test_data/bloodelffemale.m2`: vertices.count 8061, particle_emitters.count
0; `test_data/item/objectcomponents/weapon/sword_1h_artifactskywall_d_06.m2`,
chosen because it's a rare real fixture with both ribbon_emitters and
particle_emitters populated: ribbon_emitters.count 1, particle_emitters.count
2, cross-checked against `husk info`'s own prose on the same file before
writing the assertions), an undocumented chunk tag surfaces in
`chunks.undocumented_tags`, and a malformed file fails the same way
(`exit 1`, no partial JSON) regardless of `--json`. `completions/husk.{bash,zsh}`
regenerated via `--print-completion` (never hand-edited) -- `main.cpp` gained
a `zshFlagLabel` entry for `--json` so the zsh completion shows a real label
instead of falling back to the bare flag name. `README.md`'s `husk info`
section and `REFACTOR/CLI_AND_TOOLING.md` §3 updated to mark this half
done. Full suite green, 728/728 (721 + 7 new, 0 regressions).

---

## 2026-08-28 — the no-proprietary-formats clarification: the container is DDS, and I8 grows a third part

**What**: Luna clarified the decision below before any code was written against
it: husk stores **no proprietary format**. A proprietary input — M2, BLP, DB2 —
gets rehoused into something a publicly available tool can open, so husk is *the*
tool with built-in support for this data and never the *mandatory* one. Stated
explicitly for texture containers: an industry-standard container, not a raw
`.bin`.

**What it changes**: the payload half of the entry below is unaffected — the DXT
blocks are still what gets kept, still bit-exact, still never re-encoded to PNG
at ingest. What changes is the *container*: not BLP (proprietary — reading it
needs husk or a WoW-specific tool), and not a headerless block dump (no public
tool opens that). **DDS**, chosen because it holds the blocks verbatim behind a
124-byte header — a header swap, not a transcode — and because Blender, GIMP,
Pillow, Compressonator and DirectXTex all open one today.

The deciding practical fact, found rather than assumed:
`blp/src/husk_blp/decode.py:62` (`_build_minimal_dds`) already builds exactly
this container, and its own comment already notes it is "standard Microsoft DDS
layout, not WoW-specific". The transform is implemented, exercised, and known
cheap; what changes is that its output becomes a stored artifact instead of a
throwaway intermediate handed to Pillow.

KTX2 was the considered alternative and is the more modern, Khronos-owned choice
with better headroom for mip/array/cubemap cases an engine would want. It loses
on the one criterion that decided this — Blender has no native KTX2 support, and
casual viewer support is thinner. Recorded as a default, not a one-way door: the
encoding tag makes the container swappable.

**I8 restated in three parts** (`REFACTOR/README.md`), since the clarification is
project-wide and not texture-specific: nothing proprietary is stored; stored data
is human-readable or trivially transformable to it; convert on output, never on
intake. They combine into one move — keep the payload bit-exact, swap the
container for an open one, generate readable projections on demand. Note the
first part's own teeth: "not proprietary" is not the same as "openable", and the
bar is the second one.

**One consequence recorded rather than answered**: `mesh.bin` / `skeleton.bin` /
`animation.bin` in the shape sketch pass I8's readable-verb test but have never
been held to its openable-container test — they are a placeholder from before I8
existed. Marked in `BUNDLE_FORMAT.md` as a **stage-4 question**, not a blocker
for stages 1–3, to be answered the same way the texture case was. Recorded so the
placeholder is not later mistaken for a decision.

**Verified**: docs only, no code. Full suite green, 721/721, unchanged.

---

## 2026-08-28 — Luna settles texture encoding; the rule generalizes into I8

**What**: The one question the review pass below escalated is answered, and the
answer turned out to be general enough to become an invariant rather than a
texture-specific ruling.

Luna's three inputs: the canonical store should be readable and explorable;
stored data should be human-readable *or trivially transformable* to it; and the
balance to strike is readability against transform quality losses. Plus, sent
mid-pass, the reason a format is being chosen at all — raw decoded pixels are
enormous, so "just dump the bytes" was never a candidate and neither surviving
option wins on size alone.

**Decision — canonical is the source encoding; PNG is a projection husk emits on
request.** The three constraints do not actually conflict, because the
readability requirement is met by its own escape clause while the quality
requirement is only satisfiable in one direction: BLP's DXT payload decodes to
pixels deterministically (husk already ships that decoder, all five encodings,
verified against the real corpus), but PNG → DXT is a re-encode that cannot
reproduce the blocks it started from. Converting on ingest therefore spends
something irreversible to buy a convenience that was one command away — and a
canonical store that cannot reproduce its own input is not canonical. The engine
benefit (blocks are what a GPU consumes, no round trip) follows as a consequence
rather than being traded for; the decision would be the same with no engine in
the picture.

**Generalized as I8** (`REFACTOR/README.md`), because Luna stated it as applying
to *any* storage format here, not just textures: husk stores what it was given,
and readability is a transform it owes. Two teeth in it — a binary payload is
permitted only where husk has a verb that emits its human-readable equivalent
(so `mesh.bin`/`skeleton.bin`/`animation.bin` each carry a standing `husk dump`
obligation, not a licence to be opaque), and conversion happens on output, never
on intake.

**Consequences recorded**: the catalog's texture surface becomes
`texture(...) -> Resolved<EncodedTexture>` (`{bytes, encoding}`, never bare
pixels) and its decode cache becomes a *transcode* cache; a bundle texture entry
names its encoding and may carry more than one variant; the Blender addon still
needs no BLP decoder and I3 stays intact, because husk writes the PNG variant at
export time *because the target was Blender* — the same "resolve once, bake the
answer in" move `GearItem::auxGlbPath` already makes. The shape sketch's
`textures/<name>.png` is demoted to an example; the rule is that the `Ref` names
the payload and its encoding.

The only hard commitment, i.e. the expensive-to-reverse part: husk never discards
the source encoding, and PNG is never the only form it holds. Which variants a
given bundle ships stays a writer decision, changeable later without a schema
bump.

**What this deliberately does not do**: change any code. No current behavior is
wrong under this decision — `--slim-textures` and the glTF writer emit PNG
because their *target* requires PNG, which is exactly the settled shape. What
changes is where the decode is allowed to happen once `sources::Catalog` exists,
which is why this was worth settling before that object is written rather than
after. Full suite green, 721/721, unchanged.

---

## 2026-08-28 — the knowledge-base tier through `Resolved<T>`, its known-wrongness now riding provenance

**What**: `resolveObjectSkinTextureFromKb` (`cmd_export.cpp`, tier 5 —
"knowledge base" — per the previous entry's tier ordering) now returns
`husk::sources::Resolved<KbObjectSkinResolution>` instead of a bare struct.
Every early-return got a `ResolutionTier::KnowledgeBase` tag and a reason
naming *why* it missed (`--knowledge-db` not given, the file wouldn't open,
the model path isn't under `listfileRoot`, no `models` row, no
`model_object_skin_texture` row) — none of these had any diagnostic text
before this change; a caller only ever saw "0/empty, try something else."
The one hit path's reason is the exact pre-existing warning string
(`CLI_AND_TOOLING.md` §5's original fix, `1ae5bac`), moved into the
function that owns the resolution instead of reconstructed at the call
site — the call site now just does `std::cerr << "husk: warning: " <<
kbResolved.reason`.

**Why this slice**: suggested directly by the peer session doing the
concurrent Catalog-object design review (`2a34d31`'s "review pass" entry
above) as a genuinely independent, self-contained piece of §1.1 that
doesn't touch the fuzzy-pool/claim-step question that entry settled, and
doesn't depend on `CLI_AND_TOOLING.md` §3 (the real blocker for converting
the Python/Blender mirrors). `RESOURCE_CATALOG.md`'s "Where the two
unordered tiers sit" had already named this tier and ordered it (last,
since it's documented known-wrong) but nothing had wired it through
`Resolved<T>` yet.

**What this deliberately did not touch**: the SQL itself, the three-query
shape, the `--object-skin-texture-id`-takes-precedence branch above it in
`exportOneModel`, and the listfile-injection side effect
(`cmd_export.cpp`'s `listfile.emplace(...)`) `AUDIT.md`'s now-closed §1.2
already flagged as real duplication-adjacent surface left for the
`Catalog` object, not this slice.

**Verified**: full suite green, 721/721, byte-identical to before (no new
tests added — the existing `tests/test_cli_knowledge_db.cpp` pair already
asserts the exact hit-warning text and the exact miss-silence behavior at
the CLI level, and both passed unchanged, confirming this was genuinely a
verbatim-behavior move).

---

## 2026-08-28 — review pass over the staged work: four open questions answered, one escalated

**What**: No code behavior changed. A review of this session's
`[UNVERIFIED/STAGING]` commits and the questions their agents left behind,
against `REFACTOR/README.md`'s invariants and `POTENTIAL_PLAN/`'s
architectural test. Independently re-verified first rather than trusting the
commit messages: clean build, full suite green, 721/721 — matching the
newest entry's own claim.

**Answered, and moved out of "Open questions" into the owning document's
Settled section** (`RESOURCE_CATALOG.md`, `CANONICAL_MODEL.md`):

1. *Catalog owns reading or only locating?* — **reading**. The resolved path
   travels as provenance on `Resolved<T>`, not as a parallel `texturePath()`
   surface, because a second locating API needs its own tier order and two
   tier orders for one question is I2 failing again under a new name.
2. *Does `--listfile-root`'s default to `--textures` survive?* — **yes**, kept
   and stated once (it is re-derived at `cmd_export.cpp:918` *and* `:355`
   today), with `describe()` naming the effective root per I4.
3. *Tier 3's `Resolved<T>` shape* — **neither** proposed resolution. Both
   accepted that the caller has to see three outcomes; under the real
   `Catalog` object it sees one, because the claim-and-remove step mutates
   catalog-owned pool state and was never legitimately the caller's business.
   `Resolved<T>` grows an `alternates` field rather than a `std::variant`:
   the ambiguous branch already picks a single default
   (`alternateTextureCandidates.front()`), so it is a hit that knows it was a
   coin toss, not a disjoint success shape. The narrow wrap already landed is
   the correct interim and gets absorbed, not revisited. Also ordered the two
   tiers `ResolutionTier` names but the normative order never placed
   (parent-directory after tier 3; knowledge-base last).
4. *How canon distinguishes "no customization" from "no DB2 to ask"* —
   recorded **once per source, per table**, as a flat `sources` record on the
   bundle manifest; canon subtrees stay plain optionals. Availability is a
   process-level fact decided by process-level flags, so a three-state wrapper
   on every canon field would tax every field to express one fact. Per table
   rather than per run because a single 0-byte table beside good ones is this
   project's documented real failure mode (`texturefiledata.db2`).

**Escalated instead of decided** (`BUNDLE_FORMAT.md`'s new "Needs a decision
before stage 1 hardens"): **what encoding resolved texture bytes carry.**
Today the answer is implicitly decoded PNG, at the *sources* layer. WoW's BLP
is usually DXT1/3/5 — already GPU-uploadable — so a PNG-only catalog decides
"texture encoding" above the boundary `POTENTIAL_PLAN` §10 explicitly places
it below, and any later runtime backend re-decodes and re-compresses data that
arrived ready. Cheap to tag now, unrecoverable once PNG is the only thing that
ever reaches a caller. Recommendation recorded (tag the encoding, keep PNG as
the only transcode husk performs); flagged rather than taken because it changes
the stage-2 signature and the stage-4 payload rule together.

Also added one non-optional constraint to `BUNDLE_FORMAT.md`'s deliberately
open `aux/` nesting-vs-sharing call: schema v1 must allow a resource `uri`
pointing outside its own bundle directory, so measuring payload sizes later
settles it with a producer change instead of a schema bump.

**Not touched**: every stale citation of the removed "Open Questions" heading
was repointed (`AUDIT.md` §1.1, `src/sources/texture_catalog.hpp`,
`src/export_materials.cpp`) — comment text only, no code.

**Verified**: full suite green, 721/721, unchanged before and after.

---

## 2026-08-28 — tier 3 partially through `Resolved<T>`: the fuzzy-pool read step only, plus a written design question for the rest

**What**: `RESOURCE_CATALOG.md`'s Open Questions section gained a new entry
spelling out why tier 3 (fuzzy same-basename pool) can't be wrapped in
`Resolved<T>` the same mechanical way tiers 1/2 were: the real
`export_materials.cpp` orchestration has three outcomes, not two — a sole
candidate claimed and read (hit), zero candidates at all (a real miss,
falls through to the ambiguity scan), or 2+ type-compatible candidates
(a *different* success shape — every candidate embedded as an
`AlternateTextureCandidate`, not a single `T`). A bare `Resolved<T>::hit`/
`::miss` collapse would conflate "zero candidates, go scan for ambiguity"
with "claimed the sole candidate but failed to decode it, don't re-scan
the now-depleted pool" — a real behavior-preservation risk, not a
style question. Two resolutions proposed (`Resolved<std::variant<T,
AmbiguousCandidates>>`, or scope `Resolved<T>` to only the deterministic
read step and keep the three-way branch explicit in the caller), leaning
toward the second, but written up as an open question for review rather
than decided unilaterally under this loop's own timebox.

**What was still safely mechanical, and done**: the second option's
narrower half — `sources::resolveClaimedFuzzyPoolTextureBytes(claimedPath,
texturesDir, texturesOutDir) -> Resolved<FuzzyPoolTextureResult>`
(`src/sources/texture_catalog.hpp`/`.cpp`) wraps *only* the "read the bytes
of an already-claimed candidate" step, identical in shape to tiers 1/2. The
caller (`export_materials.cpp`'s fuzzy-pool block) keeps calling
`claimSoleFuzzyTextureCandidate` itself and keeps its exact original
three-way `if (fuzzy) { if (decoded) {...} } else { ...ambiguity scan... }`
structure — only the innermost `readTextureFileBytes` call is replaced,
so a decode failure still silently leaves `resolution.found` false without
ever reaching the ambiguity branch, exactly as before. 2 new tests
(`tests/test_sources_texture_catalog.cpp`): hit (real file), miss (claimed
path doesn't exist, reason names it).

**Verified**: full suite green, 721/721 (719 prior + 2 new, 0 regressions).
The real multi-candidate/ambiguity integration coverage
(`HUSK_TEST_MULTITEX_M2`/`_SKIN`-backed tests exercising the
`AlternateTextureCandidate` fan-out this change didn't touch) passed
unchanged, confirming the untouched branch really is untouched.

---

## 2026-08-28 — `AUDIT.md` §1.2 fully closed: the two name-only listfile lookups consolidated

**What**: The last two duplicated forward-lookup sites §1.2 had flagged but
not yet fixed — `export_materials.cpp`'s `gm.realContentName` assignment
and `export_extras.cpp`'s `cm.contentName` assignment, both a bare
`listfile.find(fdid)` + `.stem().string()` for display naming only (no
bytes, no path read, so neither needed `pathForFileDataId`'s root-join) —
are now one function: `sources::contentNameForFileDataId(listfile, fdid) ->
optional<string>` (`src/sources/listfile_catalog.hpp`/`.cpp`, third
function in that file alongside `fileDataIdForPath`/`pathForFileDataId`).
Both call sites now use it verbatim (the `export_extras.cpp` site keeps its
own `cm.fileDataId != 0` guard rather than pushing it into the shared
function, since `pathForFileDataId`/`fileDataIdForPath` don't special-case
fdid 0 either — consistent with the rest of this file). 3 new tests
(`tests/test_sources_catalog.cpp`): hit, empty-listfile miss, unknown-fdid
miss.

With this, `AUDIT.md` §1.2 ("FileDataID → local path") is genuinely closed:
every forward lookup this section named — the two mechanical byte/path
duplicates, the fifth site the previous entry found, and these last two
name-only ones — goes through `src/sources/listfile_catalog.hpp` now.
Removed §1.2 outright per `AUDIT.md`'s own "an item is removed when it's
fixed" convention (matching how §1.3/§11 were handled), and fixed the three
live citations that would otherwise have gone stale: `AUDIT.md`'s own
`--knowledge-db` bullet (§7's listfile-injection note), `CLI_AND_TOOLING.md`'s
matching `--knowledge-db` entry, and `RESOURCE_CATALOG.md`'s "what this
stage deletes" list (now says five implementations → one, done, rather than
four → one, not started).

**What stays open, unchanged**: `resolveObjectSkinTextureFromKb`'s
knowledge-base SQLite lookup and its listfile-map injection
(`cmd_export.cpp`'s `listfile.emplace(...)`), and the Python mirror
(`unfillable_texture_task.py`'s `_load_listfile`) — both were always
"deliberately still separate" (different backing store/language), not part
of what closed here, still real duplication-adjacent surface left for the
eventual `sources::Catalog` object.

**Verified**: full suite green, 719/719 (716 prior + 3 new, 0 regressions).
Clean rebuild, no new warnings.

---

## 2026-08-28 — second real tier through `Resolved<T>`: listfile texture lookup (`AUDIT.md` §1.1), plus a fifth §1.2 site found and fixed

**What**: `src/sources/texture_catalog.hpp` gained
`resolveListfileTextureBytes(fdid, listfile, listfileRoot, texturesOutDir)
-> Resolved<ListfileTextureResult>` — `RESOURCE_CATALOG.md`'s tier 2
(`<listfileRoot>/<real content path>`), delegating to the already-
consolidated `sources::pathForFileDataId` (§1.2) for the path and
`husk::commands::resolveTextureBytes` for the bytes; `ListfileTextureResult`
bundles `bytes` with the display `imageName` the call site also needs
(`stem.filename()`), since both come out of the same resolved path.
`export_materials.cpp`'s primary baseColorTexture listfile-fallback block
now calls it instead of the inline `pathForFileDataId` + `resolveTextureBytes`
pair. 3 new tests (`tests/test_sources_texture_catalog.cpp`): hit, miss
(no listfile row), miss (listfile row names a file that doesn't exist).

**A fifth §1.2 forward-lookup site found while doing this, not previously
named**: `export_materials.cpp`'s `additionalTextureLayers` loop
(`textureCount > 1` batches) had its *own* independent
`listfile.find(fdid)` + manual `listfileRoot / path` join + separate inline
literal-tier check — the exact same duplication `AUDIT.md` §1.2 already
named two instances of, just never counted as a third because it's a
different loop further down the same function. Found by grepping
`listfile.find(` across `src/` while confirming there was only one real
call site to migrate for tier 2 — there were two. Fixed the same way as
the primary site: now calls `resolveLiteralTextureBytes` then
`resolveListfileTextureBytes`, same tier order, verified the join-order
difference (original code stripped the extension *before* joining with
`listfileRoot`; `pathForFileDataId` joins first, then the caller strips —
extension replacement only touches the last path component, so both orders
produce an identical final path) before trusting the swap. `AUDIT.md` §1.2
updated to name this as a fifth site the original audit missed, not silently
folded into the existing count. Also flagged, but deliberately not fixed
this tick (out of this session's byte-resolution scope): two *name-only*
`listfile.find(fdid)` + `.stem()` duplicates (`export_materials.cpp:436`,
`export_extras.cpp:623`) — identical to each other, no bytes/path involved,
a real but smaller follow-up.

**Verified**: full suite green, 716/716 (713 prior + 3 new, 0 regressions).
The `additionalTextureLayers` fix has real existing coverage
(`HUSK_TEST_MULTITEX_M2`/`_SKIN`-backed integration tests) that passed
unchanged, confirming the join-order equivalence held in practice, not just
in reasoning.

---

## 2026-08-28 — first real tier through `Resolved<T>`: literal texture lookup (`AUDIT.md` §1.1)

**What**: New `src/sources/texture_catalog.hpp`/`.cpp`:
`resolveLiteralTextureBytes(fdid, texturesDir, texturesOutDir) ->
Resolved<vector<uint8_t>>` — `RESOURCE_CATALOG.md`'s tier 1 ("Literal —
`<texturesDir>/<FileDataID>.png`, then `.blp`. PNG wins when both exist"),
implemented as a thin wrapper delegating entirely to the existing, unchanged
`husk::commands::resolveTextureBytes` — no new lookup logic, only the
`Resolved<T>` provenance (`ResolutionTier::Literal`, plus a `reason` naming
the stem tried on a hit or "neither .png nor .blp exists" on a miss).
`export_materials.cpp`'s real texture-tier call site (the "Deterministic
whenever the file is actually present" block, previously calling
`resolveTextureBytes` inline) now calls this instead — same bytes, same
outcome, now behind a boundary the eventual `sources::Catalog` object can
own outright. 3 new tests (`tests/test_sources_texture_catalog.cpp`): hit,
miss-with-reason, and empty-`texturesDir`-is-a-miss.

**Why this tier, not 2 or 3 next**: tier 1 is the simplest of the three —
one deterministic file check, no shared mutable pool, no caller-specific
post-processing beyond `gm.baseColorImageName = std::to_string(fdid)`
(unchanged). Tier 2 (listfile) already has its lookup mechanism
consolidated (`sources::pathForFileDataId`, §1.2) but not wrapped in
`Resolved<T>` yet — a smaller follow-up. Tier 3 (fuzzy same-basename pool)
is the one `RESOURCE_CATALOG.md` itself flags as needing real design work
first (`claimSoleFuzzyTextureCandidate`'s claim-and-remove pool state,
`orderCandidatesForDefault`'s ranking heuristics, and the
alternate-texture-candidate fan-out for genuine ambiguity) — wrapping it
today would either lose information `Resolved<T>` can't yet express (which
of several candidates, and why) or force that design under this loop's
own timebox instead of Luna's review. Doing tiers in increasing order of
entanglement, not `RESOURCE_CATALOG.md`'s own listed order.

**What this deliberately did not touch**: tiers 2/3, the Python mirror
(`unfillable_texture_task.py`), and the Blender script's own resolution
code are all unchanged — `AUDIT.md` §1.1 stays open, now with one of its
three tiers (in the "real" C++ implementation only) reporting provenance.
No behavior change at any call site — verified, not just intended.

**Verified**: full suite green, 713/713 (710 prior + 3 new, 0 regressions).
Clean rebuild with no new compiler warnings. The touched call site's own
existing CLI-tier coverage (`tests/test_cli_textures.cpp`'s literal-FileDataID
resolution cases) passed unchanged, confirming behavior preservation at the
CLI level, not just in the new unit test.

---

## 2026-08-28 — `Resolved<T>` scaffolding for the `sources::Catalog` object (`AUDIT.md` §1.1 prep)

**What**: New `src/sources/resolved.hpp` (header-only) implementing
`RESOURCE_CATALOG.md`'s `Resolved<T>` result shape — `ResolutionTier`
(`Literal`/`Listfile`/`ParentDirectorySameBasename`/`FuzzySameBasenamePool`/
`KnowledgeBase`/`Miss`, matching the "Normative tier order" section's real
tier list, plus the parent-directory tier that section names as needing to
become "a named tier, not one consumer's private extension"), `tierName()`
for diagnostics, and `Resolved<T>::hit`/`::miss` factory functions carrying
a value-or-nullopt alongside which tier answered and a free-text `reason`.
4 new unit tests (`tests/test_sources_resolved.cpp`): hit/miss construction,
default-constructs-as-miss, and every `ResolutionTier` maps to a distinct
non-empty name.

**Why this first, not the actual tier consolidation**: `AUDIT.md` §1.1 (three
independent texture-resolution implementations across C++/Python/Blender) is
this project's own named "biggest remaining duplication," but
`RESOURCE_CATALOG.md` and this log's own "Stage 1/2 follow-up" entry are
explicit that the real consolidation needs the `sources::Catalog` object's
tier-order/ranking policy designed first, gated on a "resolution ledger diff
on real fixtures" — not something to rush in one sitting, and genuinely
cross-language (the Python/Blender mirrors can't just call new C++ directly;
`RESOURCE_CATALOG.md`'s own plan routes them through structured `husk`
output instead, `CLI_AND_TOOLING.md` §3, not started). `Resolved<T>` itself
carries zero resolution policy — it's the shape every tier will eventually
report through, same "boring, mechanically verifiable, no semantic risk"
category `db2_cache.hpp` occupied before the Catalog object existed. Landing
it now means the next real slice (migrating `export_texture_resolution.cpp`'s
tier 1/2/3 lookups to return `Resolved<T>` instead of bare `optional`) is a
mechanical wiring change against an already-reviewed type, not a type design
plus a wiring change bundled together.

**What this deliberately did not touch**: no existing function's signature
changed. `export_texture_resolution.cpp`'s three real tiers, the Python
mirror, and the Blender script's own resolution code are all still exactly
as `AUDIT.md` §1.1 describes them — this is infrastructure with no current
callers, not a fix. `AUDIT.md` §1.1 is intentionally NOT marked closed or
even partially closed by this entry; it stays open until a real tier
actually reports through `Resolved<T>`.

**Verified**: full suite green, 710/710 (708 prior + 2 new — the CMakeLists
count differs slightly from the test-case count above since some assertions
land in shared `TEST_CASE` blocks; doctest's own count is the authoritative
one). Clean rebuild via `direnv exec . cmake --build build`, no warnings
from the new header. Confirmed the one pre-existing conditional test
(`test_cli_config.cpp`'s XDG-autodiscovery case) still passes when run
without an externally-forced `HUSK_CONFIG` env var — it fails if the parent
shell's own `HUSK_CONFIG=/dev/null` leaks into its subprocess before the
test's own env manipulation, a real environment-order sensitivity in that
test, not a regression from this change (confirmed by re-running with
`HUSK_CONFIG` unset in the parent, which the test suite's own default entry
point already does).

---

## 2026-08-28 — Stage 1/2 prep: DB2 parse/resolve cache (`src/sources/db2_cache`)

**What**: New `husk::sources` namespace (`src/sources/db2_cache.hpp/.cpp`) —
the first file under `src/sources/`, the home `REFACTOR/README.md`'s target
pipeline names for stage 2. `getParsedDb2(path, dbdDir, err)` caches, per
process, the result of reading a `.db2` file's bytes, running `db2::parse`,
and resolving its WoWDBDefs table/layout/inline-column names against
`dbdDir` — returning a `const ParsedDb2*` into the cache on every call after
the first for that `(path, dbdDir)` pair.

`src/db2table.cpp`'s `readNamedColumns`/`readNamedStringColumns` — previously
two independent implementations of "read file, parse WDC5, resolve DBD
layout" — now both call through this cache instead of repeating that work.

**Why this item first**: `REFACTOR/AUDIT.md` §1.3 names this exact
duplication ("the same file from scratch to get int columns and string
columns separately") and calls the fix "free: no semantic change, just the
table being read once." It's real, scoped, and carries no resolution-policy
risk — a good first slice precisely because it's boring. It's also
infrastructure the rest of stage 2 (`RESOURCE_CATALOG.md`'s `db2(tableName)
-> const Table&`) will want regardless of how the bigger texture-resolution
consolidation (§1.1/§1.2, still not started) ends up shaped.

**What this deliberately did not touch**: the actual resolution-boundary
object (`sources::Catalog`) `RESOURCE_CATALOG.md` describes — this is one
cache the catalog will eventually own, not the catalog itself. Texture
resolution (§1.1), FileDataID→path (§1.2), and the `chrrace::load` /
`texturefiledata::load` per-call-site duplication (§1.3's other two bullets)
are all still duplicated exactly as `AUDIT.md` describes them today — this
only fixed the "same file, two readers" half of §1.3, the smallest
independently-mergeable piece. Nothing in `chrrace_db2.cpp`,
`chrcustomization_db2.cpp`, `texturefiledata_db2.cpp`, or any call site above
`db2table.cpp` changed at all; they get the caching for free on their next
call into `readNamedColumns`/`readNamedStringColumns` without knowing it
happened.

**Correctness note for future test authors**: the cache key is
`(path, dbdDir)`, held for the lifetime of the process. `tests/test_db2table.cpp`
writes synthetic fixtures to unique-per-`TEST_CASE` temp directories
(`TestDbdDir`'s own `name` argument), so no two test cases ever reuse the
same path with different content within one `husk-tests` run — verified by
inspection, not just by the suite passing. A future test that reuses a fixed
temp path across cases with *different* db2 bytes would now read stale
cached content; `husk::sources::clearDb2CacheForTests()` exists for exactly
that case and should be called at the top of any test that needs a fresh
read of a path it has written to before.

**Verified**: full suite green, 694/694 (693 prior + this session's 1 new
implicit exercise via existing `test_db2table.cpp` cases, 0 regressions).
No behavior change intended or observed — `husk export`/`husk db2-export`
CLI-tier tests exercising the DB2 chains (`chrmodel`, `chrcustomization`,
`chrrace`, `texturefiledata`, `animationdata`) all pass unchanged.

---

## 2026-08-28 — Stage 1/2 follow-up: `attachCharTextureLayout`'s own double `chrrace::load`

**What**: Traced whether last entry's cache actually closed `AUDIT.md`
§1.3 in full, per that section's own claim ("`chrrace::load` runs from
three call sites... each call re-parses"). Confirmed every DB2 consumer in
this codebase goes through `db2table::readNamedColumns`/
`readNamedStringColumns` (grepped all nine `*_db2.cpp` modules), so the
raw-parse duplication is now genuinely gone everywhere, not just for
`chrrace`. But found one real duplication the cache doesn't reach:
`attachCharTextureLayout` (`src/export_extras.cpp:656`) calls
`tryDeriveChrModelId` — which loads a full `chrrace::Data` internally
just to derive one ID and then discards it — and then, three lines later,
calls `chrrace::load` *again* on the same `(db2Dir, dbdDir)` to get the
data back. Two `chrrace::Data` builds (vector construction, joins) back to
back in one function call, every time `--chr-model-id auto` (the default)
resolves for `--char-layout-id` auto-derivation.

Fixed by giving `tryDeriveChrModelId` an optional out-param
(`std::optional<chrrace::Data>* raceDataOut = nullptr`, default keeps the
other two call sites — `attachCustomizationChoices`' two branches —
untouched) that hands back the `chrrace::Data` it already built.
`attachCharTextureLayout` now only calls `chrrace::load` itself in the
explicit-`--chr-model-id`-value branch, where `tryDeriveChrModelId` was
never called at all.

**Why this one, not §1.1/§1.2 next**: `RESOURCE_CATALOG.md`'s real
texture-resolution/FileDataID-path consolidation (§1.1/§1.2) needs the
`sources::Catalog` object itself designed first — a multi-file, semantics-
touching change this project's own gate ("resolution ledger diff on real
fixtures") says shouldn't be rushed. This was a same-shape, same-file,
mechanically verifiable follow-up already flagged by the audit, cheap to
verify in isolation.

**Verified**: full suite green, 694/694, identical assertion count to the
pre-change baseline (0 regressions). Real end-to-end smoke test against
local data (`husk export test_data/character/bloodelf/female/
bloodelffemale_hd.m2 --db2-dir /media/luna/data/wow_export/dbfilesclient
--dbd-dir reference/WoWDBDefs`): output unchanged from the historical
baseline recorded in `CLAUDE.md`'s Resume section — ChrModelID 20, layout
122, 17 default choices — confirming the removed reload was genuinely
redundant, not silently load-bearing.

---

## 2026-08-28 — Stage 1/2: closed §1.3 in `AUDIT.md`; moved the reverse FileDataID↔path lookup into `src/sources/`

**What, part 1 (doc)**: Verified §1.3's claim is now fully addressed —
every `*_db2.cpp` module goes through `db2table::readNamedColumns`/
`readNamedStringColumns` (checked all nine modules), which share the
`db2_cache` from two commits ago, and `attachCharTextureLayout`'s own
extra `chrrace::load` (previous entry) is gone too. Removed §1.3 from
`AUDIT.md` outright, per that file's own "an item is removed when it's
fixed" convention, and updated `RESOURCE_CATALOG.md`'s dangling `§1.3`
citation to point at this log instead of a section that no longer exists.

**What, part 2 (code)**: `RESOURCE_CATALOG.md`'s stage-2 surface names
`fileDataIdForPath(path) -> Resolved<fdid>` as one of the catalog's real
jobs. `AUDIT.md` §1.2 already named the one existing implementation of
that direction — `findFileDataIdForModelPath`, a file-private static in
`export_extras.cpp:312`, a linear reverse scan over the `--listfile` map.
Moved it verbatim (no logic change) to `src/sources/listfile_catalog.hpp/
.cpp` as `sources::fileDataIdForPath`, the first inhabitant of what
`RESOURCE_CATALOG.md` calls `husk::sources::Catalog`. New
`tests/test_sources_catalog.cpp` (4 cases) — this function had zero direct
unit coverage before, only reachable through the `--chr-model-id auto`
CLI path (`tests/test_cli_chrmodel_id.cpp`).

**Real bug caught in the new test, not the moved code**: the first test
draft used `/root` as a fixture `listfileRoot`. `std::filesystem::relative`
internally canonicalizes both paths, and `/root` is a real, permission-
restricted directory on this machine — the call failed with EACCES
unrelated to any logic under test, initially read as a regression. Fixed
by using a `/tmp`-rooted fixture path instead; worth noting since any
future test in this codebase building similar path fixtures should avoid
`/root`, `/boot`, and other real restricted system paths as stand-ins for
"a directory that doesn't need to exist."

**What this deliberately did not touch**: the *forward* FileDataID→path
direction (`AUDIT.md` §1.2's other three implementations —
`export_materials.cpp`'s texture-tier lookup, `exportGearAuxItemModels`,
`resolveObjectSkinTextureFromKb`'s knowledge-base path plus its listfile-
map injection). Each returns a different shape (a texture stem to try
multiple extensions against vs. a full existence-checked model path) and
consolidating them for real needs the actual `sources::Catalog` object's
tier-order design (`RESOURCE_CATALOG.md`'s "Normative tier order"), not a
superficial merge. §1.2 stays open in `AUDIT.md`, now missing only its
first bullet.

**Verified**: full suite green, 698/698 (694 prior + 4 new, 0
regressions). `tests/test_cli_chrmodel_id.cpp`'s existing FileDataID-
primary-path coverage (the Dracthyr disambiguation case) passed unchanged,
confirming the move preserved behavior at the CLI level too, not just in
the new unit test.

---

## 2026-08-28 — `CLI_AND_TOOLING.md` §2: the missing `none` state for `--db2-dir`/`--dbd-dir`/`--listfile`/`--listfile-root`

**What**: `AUDIT.md` §7 and `CLI_AND_TOOLING.md` §2 both named a real,
scoped gap distinct from the "three grammars" observation they otherwise
just explain: since `--config`/`$HUSK_CONFIG` TOML defaults landed, a
config-supplied `db2-dir`/`dbd-dir`/`listfile`/`listfile-root` on `husk
export` had **no per-invocation opt-out** — the evidence cited was that
`tests/run_husk.hpp` has to blank the *entire* config
(`HUSK_CONFIG=/dev/null`) on every subprocess spawn just to get a clean
run. Added the literal value `'none'` to all four, same convention
`--textures`/`--skin-dir`/`--bones-dir` already use: `db2Dir`/
`dbdDirForChr` (`cmd_export.cpp`) now clear to empty on `'none'`, the
`--listfile` load is skipped outright, `listfileRoot` clears too. Fixed
the stale `--db2-dir`/`--dbd-dir` comment `AUDIT.md` §9 also flagged
("husk has no way to derive a layout ID on its own" — untrue since
2026-08-21's `--char-layout-id` auto-derivation) in the same edit, since
it sat directly above the code this touched.

**Scope decision**: `--db2-dir`/`--dbd-dir` are also registered on
`db2-build` (all three `->required()` there — no off-state is meaningful
for a command that can't do anything without them), `db2-info`, and
`appearance-string` (neither of which ever got `--config` wiring in the
first place, so neither has a config-supplied default to opt back out
of). Only `export`'s own instances gained `'none'` — extending it to the
others would be solving a gap that doesn't exist for them.

**Shell completions**: `bashValueCompletion`/`zshValueAction`
(`src/main.cpp`) are keyed only by flag *name*, shared across every
subcommand's own flag table — naively adding `'none'` there would have
offered it as a completion for `db2-build --db2-dir` too, where it isn't
actually honored. Made both functions take the subcommand name as a
second parameter so the `'none'`-offering branch can check `subName ==
"export"` before firing; `db2-build`'s own `--db2-dir`/`--dbd-dir`/
`--listfile` completions are unchanged (directory/file completion only,
no `'none'` word). `completions/husk.{bash,zsh}` regenerated via
`--print-completion`.

**New tests** (`tests/test_cli_config.cpp`, 2 cases): config supplies
`db2-dir`/`dbd-dir` pointing at real-but-empty directories, asserting the
customization-DB2-resolution attempt fires (a real "...DB2 data resolved
from..." stderr note) without `'none'` and doesn't fire with `--db2-dir
none --dbd-dir none`; same shape for `--listfile none` against a
config-supplied listfile path. **A real gotcha found writing these**: the
first draft used `tinyValidM2()` alone, which has zero inline bones —
`attachCustomizationChoices`/`attachCharTextureLayout` are both gated
behind `cmd_export.cpp`'s `if (!bones.empty())` block, so neither ever
ran and the assertion silently checked the wrong thing (a `MESSAGE()`
dump of the real subprocess output caught this, not guesswork). Fixed by
adding a same-basename `.skel` sidecar (`boneCorrectionSkel()`,
`tests/test_cli_fixtures_scenes.hpp` — the exact fixture
`tests/test_cli_chrmodel_id.cpp`'s own DB2 tests already use for the same
reason).

**Verified**: full suite green, 700/700 (698 prior + 2 new, 0
regressions). Real end-to-end smoke test against `bloodelffemale_hd.m2`
with real local DB2 data: `--db2-dir none --dbd-dir <real dir>` produces
the exact same three "--db2-dir/--dbd-dir are required... skipping"
notes a fully-unset run would, and a clean export (exit 0, 195498
vertices/45418 triangles/245 bones/338 clips) — confirming `'none'`
disables the feature even when a *different* real flag in the same
group is still given, not just when both are absent.

---

## 2026-08-28 — Three subagent tasks: README.md's stale Blender-addon
claim fixed, `AUDIT.md` §9/§10 closed/flagged, §1 flag grouping in
progress

Spawned three isolated-worktree subagents in parallel on independent
`AUDIT.md` items, per Luna's own request, then reviewed and merged each
by hand rather than trusting their own "done" claims.

**Agent 1 (DESIGN.md/README.md non-goal fix) — landed, half redundant**:
the agent's worktree was branched from `19e3ab6`, several commits behind
`master` — it never saw `31cfff7` ("Add REFACTOR/..."), which turned out
to have *already* fixed `DESIGN.md`'s stale "No Blender addon" non-goal
that same day, with different (strikethrough + "Superseded, 2026-08-28")
phrasing. The agent's `DESIGN.md` diff would have duplicated/conflicted
with that, so it was **discarded**. Its `README.md` fix (the Roadmap
section's identical stale claim — "husk doesn't write a Blender addon
itself... Blender-side concerns... are explicitly out of scope" — which
`31cfff7` never touched) was still genuinely needed and **was applied**,
by hand, adapted to point at `DESIGN.md`'s now-different phrasing rather
than cherry-picking the agent's commit verbatim. `AUDIT.md` §9 (both
bullets — the Blender-addon claim and the separately-already-fixed
`cmd_export.cpp` layout-ID comment from two commits ago) is now fully
resolved and removed.

**Agent 2 (file-size ceiling exception comments) — correctly refused,
real blocker found**: the task asked for one-line exception comments per
`FILE_SIZE.md` §4's format. The agent discovered `FILE_SIZE.md` **does
not exist anywhere in this repo's history** (`git log --all
--diff-filter=A -- FILE_SIZE.md` — never committed, on any branch) —
despite being cited as real by `AUDIT.md` §10 itself,
`REFACTOR/BLENDER_ADDON.md:46`, and two `tests/test_integration*.cpp`
files. Rather than fabricate a plausible-sounding exception-category
taxonomy that might contradict whatever was actually intended, the agent
stopped and reported the blocker — the right call. `AUDIT.md` §10 updated
to state this explicitly: the *policy* (1000-line ceiling, §4 comment
requirement) is real and consistently referenced, but the document that
was supposed to define it never got written. This item can't close via
comment-writing alone; it needs `FILE_SIZE.md` authored first, which is
Luna's call, not something to guess at autonomously.

**Agent 3 (`->group()` flag taxonomy, `CLI_AND_TOOLING.md` §1) — reapplied
by hand, not merged**: same root problem as Agent 1 — this worktree also
branched from `19e3ab6`, so its `src/cmd_export.cpp` predates every commit
this session made to that file (`ae04e39`'s `'none'` support and help-text
updates, most recently). Rather than resolve a diverged three-way merge,
took the agent's own grouping decisions (verified correct against
`CLI_AND_TOOLING.md` §1's table) and reapplied `->group("...")` to all 27
call sites (26 `add_option`/`add_flag` + the `--config` `set_config()`) by
hand on the current file. Two real discrepancies the agent found and
flagged, both confirmed independently before trusting them: `--textures`
exists in code but isn't in the table at all (placed under "Input /
output" — the agent's own judgment call, kept, since it sits with
`--textures-out`/`--slim-textures` and controls I/O-adjacent behavior, not
resolution policy the way `--skin-dir`/`--bones-dir` do); `--format` is in
the table but doesn't exist anywhere in the code (left alone — not
invented).

**Verified independently** (not just the agent's own claims): full suite
green, 700/700 in the real environment (694 + tonight's various additions,
0 regressions) — the agent's own reported "643/643, 52 skipped" was a
fixture-availability artifact of running from an isolated worktree missing
several `HUSK_TEST_*`-relevant symlinks/env, not a real discrepancy.
`husk export --help` shows all 7 group headers (`Diagnostics`, `Input /
output`, `Batch`, `Model sidecars`, `Game data`, `Character`, `Appearance /
gear`) in registration order. Diffed `--print-completion=bash`/`=zsh`
output byte-for-byte before and after — confirmed identical, so
`completions/` correctly needed no regeneration (the generator walks
`get_options()` directly, not the formatted `--help` groups).

**Overall verified**: full suite green, 700/700, after all of this
entry's changes (the README.md fix, both `AUDIT.md` trims, and the flag
grouping) landed together.

---

## 2026-08-28 — `AUDIT.md` §10 unblocked: `FILE_SIZE.md` symlinked in,
real per-file exception comments added

Luna corrected the "`FILE_SIZE.md` doesn't exist" finding from the
subagent above: it exists at `~/nix/claude-rules/FILE_SIZE.md`, the same
home this repo's own `.claude/rules/nix.md` already symlinks from for
Nix conventions. Symlinked `FILE_SIZE.md` (repo root) →
`~/nix/claude-rules/FILE_SIZE.md`, matching that established convention
— every citation in this codebase (`AUDIT.md`, `BLENDER_ADDON.md`,
`tests/test_integration*.cpp`) already refers to it as a bare
repo-root-relative name.

With the real policy doc in hand, read it properly (its §3 names three
genuine exceptions: a chain-not-hub-and-spoke split, one sequential
process with no real seams, or a flat list of many small identically-
shaped items — and its §4 is explicit that the required comment is "not
a paragraph — a pointer") and checked each of the four oversized files'
actual structure against those three categories rather than writing a
generic placeholder for all four:

- `src/export_extras.cpp` — genuinely fits §3's third exception: ~9
  independent, identically-shaped `attachX()` enrichment functions, each
  readable alone.
- `src/cmd_export.cpp` — a real mix, not one clean fit: `addExportOptions`/
  the `resolve*` helpers are §3's third exception (flat, independent),
  but `exportOneModel` itself is §3's second (one genuinely sequential
  per-model pipeline). Said so explicitly rather than picking one
  arbitrarily.
- `tools/husk_blender_geoset_mask.py` / `tools/corpus_scan_tasks/render_glb.py`
  — neither fits any of the three genuine exceptions honestly; both are
  oversized because real addon-module packaging (`REFACTOR/BLENDER_ADDON.md`)
  hasn't happened yet. Said that plainly instead of forcing a false
  exception claim — `FILE_SIZE.md` §2's own default ("if none of these
  genuinely apply... split it") makes clear an exception should never be
  claimed just to satisfy the ceiling-comment requirement.

One-line comments (not paragraphs, per §4) added near the top of all
four files, each naming its real disposition and, where relevant, which
`REFACTOR/` stage is expected to resolve it.

**Verified**: full suite green, 700/700, 0 regressions (C++ files
rebuilt and tested; both Python files independently confirmed to still
parse via `ast.parse` through `tools/venv`'s own interpreter). `AUDIT.md`
§10 removed outright now that it's genuinely closed, not just
worked around.

---

## 2026-08-28 — `AUDIT.md` §11 closed: `missing_texture_task.py` deleted,
`shader_id_task.py`'s stale claim corrected (partially — real blocker
found)

**`missing_texture_task.py`**: superseded by `unfillable_texture_task.py`
per its own module doc, but still present and runnable — the exact
"future session picks the wrong one" risk `CLAUDE.md`'s own Hazards
section already had to warn about. Moved to `./trash/` (not `rm`, per
this loop's own instruction). Verified no code depends on it (grep across
`tools/` found only comment-level "same cost profile as"/"matches this
convention" mentions, no imports) before moving it, then fixed every
surviving reference rather than leaving them dangling: `tools/CORPUS_SCANS.md`'s
task table (row removed) and its own "rule going forward" paragraph
(rephrased to cite the deleted file as historical precedent, not a live
example); `particle_only_task.py`/`casc_size_mismatch_task.py`'s
comparison comments repointed at `unfillable_texture_task.py` (dropped
one comparison entirely rather than inventing a citation
`unfillable_texture_task.py` doesn't actually support — the `_unresolved/`
FileDataID convention isn't documented in either survivor, only casc-tool's
own README); `unfillable_texture_task.py`'s own docstring updated to
describe superseding a deleted file rather than differing from a live
one; `TOOLS.md`'s tool catalog (entry removed, it's current-state
documentation, unlike `CLAUDE.md`'s session-narrative mentions which were
left alone as accurate history); `TODO/RENDER_QUALITY_TODO.md`'s one
cross-reference repointed.

**`shader_id_task.py`**: confirmed the stale claim directly — its
docstring said *"husk currently parses no field of M2Batch's on-disk
shader_id"*; `src/skin.hpp:70`/`skin.cpp:195` (`Batch::shaderId`) and
`src/m2_shader_names.hpp` (`resolveShaderNames`) both do, and the exact
byte offsets the Python task hand-parses (`_SHADER_ID_OFFSET = 0x02`,
`_TEXTURE_COUNT_OFFSET = 0x0E`, `_BATCH_STRIDE = 0x18`) were checked
directly against `skin.cpp`'s real `parseBatches` and confirmed to match.
Fixed the false claim. **But the reclassification `RESOURCE_CATALOG.md`
itself predicted ("almost certainly structured output") turned out
premature, not just a rename**: grepped `cmd_info.cpp`/`cmd_dump.cpp` for
any consumer of `shaderId`/`resolveShaderNames` and found none — both are
used *only* internally by `export_materials.cpp`. Neither `husk info` nor
`husk dump-chunks` exposes this field or its resolved name anywhere in
their own output today, so there is currently nothing structured for the
corpus task to consume instead of reading raw bytes. Rewrote the
docstring to state this explicitly, satisfying `RESOURCE_CATALOG.md`'s
own excavation-escape-hatch rule ("a task that reads raw bytes must say
in its own docstring which husk understanding it is deliberately going
behind") — the raw read is now a documented, considered exception, not
an unconverted leftover, pending `CLI_AND_TOOLING.md` §3's structured-
output work (`husk info --json`/`husk resolve`, not started).
`shader_names_task.py` was checked too (same underlying fact) and found
to already document this correctly — no fix needed there, confirmed by
reading rather than assumed from `RESOURCE_CATALOG.md`'s table alone.

`RESOURCE_CATALOG.md`'s own corpus-task classification table and
`CLI_AND_TOOLING.md` §4's "Also" bullets updated to match the real,
partial disposition (docstring fixed; reclassification blocked on real,
cited, unstarted work) rather than left claiming a clean "Delete"/
"Reclassify" that oversimplifies what's actually true now. `AUDIT.md`
§11 removed outright, both items genuinely resolved.

**Verified**: full suite green, 700/700 (unaffected by this entry --
C++ untouched). All four edited Python files independently confirmed to
still parse via `ast.parse`. Swept for dangling `missing_texture_task`
references across the whole repo afterward; every remaining hit is a
deliberate historical/explanatory mention, not a live dependency.

---

## 2026-08-28 -- close AUDIT.md §8: corpus-tooling CORPUS_ROOT/HUSK_BIN/LISTFILE duplication

**What**: `corpus_scan_framework.py` now exposes `ROOT`, `LISTFILE`, and
`HUSK_BIN` as single, dynamically-read module-level values instead of each
task hardcoding its own copy. `ROOT`/`LISTFILE` are set once per run by
`_init_worker` (from the real `--root`/`--listfile` the run was launched
with -- `--listfile` is a new framework CLI flag, previously nonexistent);
`HUSK_BIN` is computed once at import time. `black_additive_task.py`,
`casc_size_mismatch_task.py`, `unfillable_texture_task.py`,
`texture_dedup_collision_task.py`, `m2_full_validation_task.py`, and
`particle_only_task.py` had their own `CORPUS_ROOT`/`HUSK_BIN`/`LISTFILE`
constants deleted and now read `corpus_scan_framework.ROOT`/`.LISTFILE`/
`.HUSK_BIN` (aliased as `csf` at import) instead.
`corpus_checks.py`'s own separate `HUSK_BIN` default (used only by
`m2_full_validation_task.py` and `corpus_checks_example.py`) was fixed at
its one real source rather than overridden per-caller.

**Why**: `CLI_AND_TOOLING.md` §4 named this as three distinct duplication
bugs (`CORPUS_ROOT` in 6 modules, `HUSK_BIN` in 7, `LISTFILE` in 5) and
prescribed subtraction, not relocation -- a task should never declare its
own copy of something the framework already knows.

**Two real bugs found and fixed while implementing this, neither
hypothetical -- both caught by a real smoke-test run against 40 live
corpus files, not assumed from reading the doc**:

1. `CLI_AND_TOOLING.md` §4's own prescribed fix for `HUSK_BIN` (a bare
   `"husk"`, trusting its own claim that the flake dev shell puts husk on
   `PATH`) fails outright on the real environment -- verified live,
   `.direnv/bin` carries no `husk` symlink. Luna's own correction after
   this landed: not a dev-shell bug -- installing the flake as a package
   (`nix profile install`/`nix run`) does put `husk` on `PATH`, that's
   just not the dev-shell environment this corpus tooling actually runs
   under (a dev shell hands you the build tools, not the project's own
   not-yet-rebuilt output, by design). Fixed with
   `shutil.which("husk") or str(REPO_ROOT / "build" / "husk")`, the same
   `shutil.which`-first idiom `corpus_checks.py` already used for
   `GLTF_VALIDATOR_BIN` -- PATH first (correct for a package install),
   falling back to the known-good local build path (correct in the dev
   shell today).
2. Every task module's own docstring documents running
   `corpus_scan_framework.py` directly as a script
   (`python tools/corpus_scan_framework.py --task ...`), which loads it as
   `__main__` -- a *different* module object from the
   `corpus_scan_framework` a task gets via its own
   `import corpus_scan_framework as csf`, with independent globals.
   `_init_worker` was setting `ROOT`/`LISTFILE` on whichever identity
   actually ran, while every task read them off the other, untouched,
   still-`None` one (`AttributeError: 'NoneType' object has no attribute
   'exists'`). Fixed with one `sys.modules.setdefault("corpus_scan_framework",
   sys.modules[__name__])` at module load, so both names always resolve to
   the same object regardless of which one loaded first. `_init_worker`
   was also reordered to set `ROOT`/`LISTFILE` *before* importing the task
   module (a task's own module-level code, e.g. `m2_full_validation_task.py`'s
   `cc.CORPUS_ROOT` reconciliation, can only see the real value if it's
   already set by then) -- though `m2_full_validation_task.py` itself was
   further changed to set `cc.CORPUS_ROOT` lazily inside `analyze()`
   rather than at module import time regardless, since under
   `ProcessPoolExecutor`'s default fork start method a forked worker
   inherits the parent's already-executed top-level code rather than
   re-running it, so import-time assignment alone isn't reliable across
   start methods.

**What was deliberately not touched**: `render_sample_driver.py` -- a
driver script with its own argv (not a `ScanTask` invoked through
`corpus_scan_framework.py --task`), and part of the render pipeline this
project has repeatedly treated as human-gated (`CLAUDE.md` Hazards,
`feedback_knowledge_base_render_human_gated` memory). It still has its own
`CORPUS_ROOT`/`HUSK_BIN`/`LISTFILE` copies -- a real follow-up if that
pipeline is ever folded into this same mechanism, not attempted here.
`verify_appearance_string_pipeline.py` and `corpus_test.py`'s own
hardcoded `HUSK_BIN` copies were also left alone: the former is a
standalone build-check script with an intentionally different design (a
friendly `.exists()` check plus a build hint, not the same disease), and
the latter is `corpus_checks.py`'s own docstring-documented "kept as-is,
untouched, reference for its own now-superseded approach" predecessor.

**Verified**: full suite green, 700/700 (C++ untouched by this entry).
All touched Python files confirmed to still parse via `ast.parse`.
Real end-to-end smoke tests against live local corpus data (not just
syntax checks) for every touched task, each with `--limit` against
`/media/luna/data/wow_export`, zero errors in every case:
`unfillable_texture_task` (40 files), `black_additive_task` (40),
`casc_size_mismatch_task` (40, thread-mode, exercises the real
`casc-tool list` subprocess), `texture_dedup_collision_task` (30),
`particle_only_task` (30), `m2_full_validation_task` (10, the task with
CLEANUP_TODO.md's own known full-corpus-scale hang -- deliberately kept
to a small `--limit`, consistent with that item's own note that small
bounded runs are clean). Also re-ran one untouched task
(`expansion_task`, 20 files) through the now-modified shared framework to
confirm the framework-level changes didn't regress a task that never
touched `ROOT`/`LISTFILE`/`HUSK_BIN` at all. `AUDIT.md` §8 and
`CLI_AND_TOOLING.md` §4 updated to reflect the real, verified disposition
(including both bugs found, and the deliberate `render_sample_driver.py`
exception) rather than the original speculative fix description.

---

## 2026-08-28 -- correct HUSK_BIN doc wording per Luna's live correction

**What**: Luna corrected the previous entry's framing immediately after it
landed: "the flake does put the husk in the path, if the flake is
installed, not so in the dev shell." The previous entry's docs described
`CLI_AND_TOOLING.md` §4's original "the flake dev shell already puts husk
on PATH" claim as simply wrong -- overstated. It's accurate that
installing the flake as a package (`nix profile install`/`nix run`) does
put `husk` on `PATH` via the normal Nix mechanism; what's actually true
(and what the live smoke test caught) is narrower: the *dev shell*
(`direnv exec .` / `nix develop`) doesn't, by design -- a dev shell hands
you the tools to build the project, not the project's own not-yet-rebuilt
output. Corrected the framing in `AUDIT.md` §8, `CLI_AND_TOOLING.md` §4,
and `corpus_scan_framework.py`'s own `HUSK_BIN` comment. This file's
previous entry is left as-is (historical record, not edited after the
fact).

**Why**: precision matters here specifically because the wrong framing
would have pointed a future reader at "fix the flake's dev shell" as the
real fix, when that's not actually broken for its own purpose --
the `shutil.which`-first fallback already committed is correct exactly
because both cases (package-installed vs. dev-shell-only) are real and
need to be handled, not because one of them is a bug to fix upstream.

**Verified**: comment/doc-only + one code-comment change in
`corpus_scan_framework.py`; re-parsed via `ast.parse`, no behavior change
(the `shutil.which(...) or <build path>` logic itself is untouched).

---

## 2026-08-28 -- close AUDIT.md §7/CLI_AND_TOOLING.md §5: `--knowledge-db`'s known-wrongness now surfaces at point of use

**What**: `src/cmd_export.cpp`'s `--knowledge-db` handling now prints a
real `std::cerr` warning at the exact point a knowledge-base resolution
is about to be used (naming the resolved texture FileDataID and pointing
at `TODO/KNOWLEDGE_BASE_DESIGN.md`), instead of the flag's documented
known-wrongness (same-slot cross-item collisions, 15/15 real spot checks
wrong-item) living only in `CLAUDE.md`'s Hazards section -- a note a
caller of this flag might never read. First CLI-tier test coverage this
flag has ever had: `tests/test_cli_knowledge_db.cpp`, a new file with a
minimal synthetic SQLite fixture built directly against the exact three
raw tables/columns `resolveObjectSkinTextureFromKb` queries (not `husk
db2-build`'s real view/join machinery, which the runtime code doesn't
care about), covering both a real resolution (warning printed, names the
right FileDataID) and a miss (no warning, matching the function's own
"empty/0 is a real, expected case" contract).

**Why**: `CLI_AND_TOOLING.md` §5 framed this as needing an explicit
decision -- retire the flag, or keep it with its known-wrongness surfaced
at point of use (I4). Checked `TODO/KNOWLEDGE_BASE_DESIGN.md` first
rather than deciding fresh: that file already made this exact call
("kept as diagnostic/future-work infrastructure, not load-bearing";
`render_sample_driver.py` deliberately never passes `--knowledge-db`).
Retiring the flag here would have silently reopened an already-settled
decision instead of executing it -- the actual gap was narrower than "no
decision has been made," it was "the decision that *was* made was never
wired into the CLI's own output."

**What was deliberately not touched**: the flag still mutates the shared
listfile map mid-export to let the embed path pick up its answer
(`cmd_export.cpp`'s `listfile.emplace(...)` call) -- a real instance of
`AUDIT.md` §1.2's FileDataID→path duplication. Fixing that means the
catalog (`RESOURCE_CATALOG.md`, not built), not a local patch here.

**Verified**: full suite green, 702/702 (700 + 2 new, 0 regressions).
Both new tests independently confirmed passing in isolation
(`-tc="*knowledge-db*"`) before the full-suite run. `AUDIT.md` §7 and
`CLI_AND_TOOLING.md` §5 updated to the real, verified disposition.

---

## 2026-08-28 -- partial AUDIT.md §1.2: forward FileDataID -> path consolidation (2 of 4 sites)

**What**: `src/sources/listfile_catalog.hpp`/`.cpp` (already home to
`fileDataIdForPath`, the reverse-direction lookup from an earlier
session's §1.3 close) gained `pathForFileDataId(listfile, listfileRoot,
fdid) -> optional<path>` -- the forward direction. Two of the four sites
`AUDIT.md` §1.2 named now call it instead of duplicating `listfile.find(fdid)`
+ a `listfileRoot` join inline: `export_materials.cpp`'s texture-tier
`--listfile` fallback, and `cmd_export.cpp`'s `exportGearAuxItemModels`.
Each caller's own post-lookup behavior (extension stripping in one,
an explicit existence check + diagnostic text in the other) stayed
exactly where it was -- only the identical "look it up" step moved.

**Why**: continuing the same incremental pattern §1.3's reverse-direction
move already established, rather than waiting for the full
`sources::Catalog` object `RESOURCE_CATALOG.md` describes (multi-session
work, not a single loop tick) -- this is explicitly framed there as the
"free half of stage 2" category db2_cache.hpp already used: consolidating
duplicated *mechanism* now, leaving duplicated *policy* (ranking,
provenance/`Resolved<T>`, caching) for when the real catalog object is
built.

**A real behavior-preservation subtlety caught before landing, not
assumed**: the two call sites disagreed on whether an empty
`listfileRoot` was even checked. `exportGearAuxItemModels` already
early-returns before ever reaching the lookup if `listfileRoot` is empty;
`export_materials.cpp`'s site never checked that at all, relying on
`std::filesystem::path("") / rel` acting as an identity join for a caller
that passes `--listfile` without `--listfile-root`. A first draft of
`pathForFileDataId` early-returned `nullopt` on an empty `listfileRoot`
unconditionally -- which would have been a real, if obscure, behavior
change for that specific edge case (the fallback tier silently stops
firing instead of trying a CWD-relative path). Fixed by dropping that
check from the shared helper entirely (only `listfile.empty()` is
checked) -- `exportGearAuxItemModels` still gets the same effective
guarantee via its own existing early return, and `export_materials.cpp`
gets its exact original behavior back.

**Deliberately left alone**: `resolveObjectSkinTextureFromKb`'s
knowledge-base SQLite lookup (a genuinely different backing store, not
the same duplication) and its listfile-map injection
(`cmd_export.cpp`'s `listfile.emplace(...)` after a KB hit -- itself
duplication-adjacent surface, but a real catalog-object question, not a
verbatim-move one); `unfillable_texture_task.py`'s own Python
`_load_listfile`, outside this C++ consolidation's reach entirely.

**Verified**: full suite green, 706/706 (702 + 4 new, 0 regressions) --
4 new unit tests in `tests/test_sources_catalog.cpp` for
`pathForFileDataId` itself (a real join, empty-listfile miss, unknown-fdid
miss, and the empty-listfileRoot identity-join case specifically,
regression-testing the subtlety above), plus the full existing
`test_cli_textures.cpp`/`test_cli_gear_export.cpp` suites (both call
sites' own real CLI-tier coverage) passing unchanged -- confirms
behavior preservation end-to-end, not just at the new unit's own level.
