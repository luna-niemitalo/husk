# REFACTOR/ — target pipeline and cleanup punch list

**This directory describes a target, not the current tree.** Nothing in it is
implemented. `DESIGN.md` remains the authority on why today's code is shaped the
way it is; this is where the shape it's moving toward lives, kept separate on
purpose (`READABILITY.md` §3.10 — current and target never blurred).

Same conventions as `TODO/`: each file is an open punch list, closed items get
removed outright, git history is the record.

## Why

husk grew outward from `cmd_export.cpp` rather than down through named stages.
The result, in Luna's own framing: *"different parts search different files and
different methods from different directions, and then we have situations where 3
different pieces resolve the same data in 3 slightly different incorrect ways."*

That is literally measurable — texture-slot resolution alone has four
independent implementations that already disagree, and one of them silently
dropped a resolution tier for weeks without any test noticing. `AUDIT.md` is the
full inventory with file:line evidence.

The goal is not more features. It is that each question has exactly one place
that answers it, and each consumer has exactly one place to look.

## The target pipeline

Four stages, in Luna's own words: *input data parsing → adjacent data resolution
→ internal representation → converting the data to target format*.

```
   .m2 .skin .skel .bone .anim .phys .blp .db2 .dbd  listfile
                          │
                          ▼
   ┌──────────────────────────────────────────────┐
   │ 1. FORMATS   src/formats/                    │  bytes → structs
   └──────────────────────┬───────────────────────┘
                          ▼
   ┌──────────────────────────────────────────────┐
   │ 2. SOURCES   src/sources/                    │  where do the bytes live
   └──────────────────────┬───────────────────────┘
                          ▼
   ┌──────────────────────────────────────────────┐
   │ 3. CANON     src/canon/                      │  what does it mean
   └──────────────────────┬───────────────────────┘
                          ▼
   ┌──────────────────────────────────────────────┐
   │ 4. WRITERS   src/writers/                    │  how is it delivered
   └──────┬──────────────────────┬────────────────┘
          ▼                      ▼
     husk bundle            glTF (.glb)
     (primary)              (optional projection)
          ▼
     Blender addon
```

| Stage | Home | Owns | Must not contain |
|---|---|---|---|
| 1. input parsing | `src/formats/` | bytes → structs: m2, skin, skel, bone, anim, phys, blp, db2, dbd | filesystem search, defaults, policy |
| 2. adjacent resolution | `src/sources/` | *where* bytes live: the catalog, listfile, sidecar conventions, DB2 table cache | semantic interpretation |
| 3. internal representation | `src/canon/` | meaning: definition / selection / semantic resources | paths, tinygltf, bpy, GPU types |
| 4. target conversion | `src/writers/` | bundle (primary), glTF (projection), JSON (info/dump) | re-resolving anything |

## Invariants

Stated once here. Every other file in this directory references them by number
rather than restating them.

**I1 — no paths in `canon::`.** Stage 2 resolves; canon holds semantic resource
references; the *writer* is what mints relative URIs. If a `std::string` holding
a filesystem path appears in a `canon::` struct, the boundary has leaked.

**I2 — one implementation per resolution question.** Two callers needing the
same answer both call the catalog. No mirrors, no "second opinion"
reimplementations inside the product. (Corpus *excavation* tooling is a
deliberate, scoped exception — see `RESOURCE_CATALOG.md`.)

**I3 — a consumer opens what the manifest names, and nothing it does not.** No
directory searching, no "extra data dir" parameter, no shelling out to husk.
Following a reference the manifest itself declares — including one pointing at
another bundle's manifest, see `BUNDLE_FORMAT.md` — is not an exception to this;
it is the rule working. The distinction is between being *told* where something
is and going *looking* for it. `GearItem::auxGlbPath`
(`src/cmd_export.cpp:695-705`) is the existing good precedent: husk resolves
once and bakes a relative path in. Generalize it; don't reinvent it per feature.

**I4 — resolution is self-announcing.** Which tier fired, from which directory,
via which fallback, is a *read operation* (`CLI.md` §2.4/§2.11), not something
inferred from an export summary after the fact. This is also what makes the
migration gates below affordable to run.

**I5 — definition ≠ selection.** POTENTIAL_PLAN §4: "NightElf.face[12]" (what
option 12 *is*) and "this character chose 12" are different kinds of fact. Today
`chr_customization_options` (definition) and `enabled_geosets` (selection) are
sibling extras keys with no structural distinction between them.

**I6 — identity is an ID; a name is decoration with a stated source.** Most
human-readable naming here comes from the community listfile (unreliable at
best) or is husk-synthesized. Every reference carries its real identity *and*
where its name came from, so a downstream consumer can tell a listfile guess
from a DB2 fact from a string embedded in the M2 itself.

**I7 — item identity is not slot identity.** One appearance item may occupy more
than one equipment slot and contribute more than one mesh/geoset region — a
tunic whose hem is a leg-slot piece is one garment, not a chest thing and an
unrelated legs thing. Canon models `item → {slots[], components[]}` so a
consumer can eventually load *the tunic*, not "the chest half of the tunic".
**Preserving this possibility is required; implementing outfit-level loading is
not.** The constraint is only that nothing in the model may make it impossible
later.

## No gate is "output is unchanged"

Current exports have never been visually correct. Byte-identity would therefore
enshrine the existing wrongness as the acceptance criterion for a refactor whose
whole point is giving that wrongness fewer places to hide.

Every gate below is instead **"every difference is explained and attributed"**.
A difference that turns out to be a fix is a result to record, not a regression
to revert. Part of the hope for this work is precisely that consolidating four
drifting implementations into one exposes *why* things look wrong today.

## The documents

| File | Scope | Gate |
|---|---|---|
| `AUDIT.md` | Evidence inventory: every duplicated, divergent, or counter-intuitive path, with file:line | Independent |
| `RESOURCE_CATALOG.md` | Stage 2 — the one resolution boundary object, and the excavation escape hatch | Independent |
| `CANONICAL_MODEL.md` | Stage 3 — the semantic model, built pure | Independent to design; the migration itself is the expensive stage |
| `BUNDLE_FORMAT.md` | The native husk bundle + manifest schema; glTF's demoted role | Independent |
| `BLENDER_ADDON.md` | Stage 4 consumer — packaging, and deleting every discovery mechanism I3 forbids | Independent to build; final visual pass is Luna's |
| `CLI_AND_TOOLING.md` | Flag surface, structured output, corpus-tooling cleanup | Independent |

Out of scope, deliberately: everything in `POTENTIAL_PLAN/` about engines, GPU
backends, and GFX906. The canonical model is designed so those stay *possible*
(POTENTIAL_PLAN §10's architectural test), not so they happen next. Current
trajectory is `M2 → canonical → Blender`.

## Migration order

Each stage lands independently. Ordered so the highest-drift-risk duplication
dies first, and nothing waits on the bundle format.

**1. Catalog** (`src/sources/`) — collapse the four texture-resolution and four
FileDataID→path implementations onto one object; add `describe()`.
*Gate*: full suite green, plus a **resolution ledger diff** on real fixtures
(`bloodelffemale_hd`, `nightelffemale_hd`, a creature, an item): for every
texture slot, which tier fired and which file it resolved to, before vs. after.
Deltas are expected — the four implementations disagree today, that *is* the
finding — so the gate is that every delta is individually attributed
("implementations disagreed, catalog uses the documented tier order" / "this is
a fix"), not that the count is zero. I4's `describe()` is what makes this ledger
cheap enough to actually run.

**2. `m2::Model` aggregate** — the three commands stop each assembling their own
partial view.
*Gate*: `husk info` / `dump-chunks` output diffed on real fixtures, every
difference attributed. The three commands parse genuinely different subsets
today, so unifying them can legitimately surface fields one of them was silently
omitting.

**3. `canon::`, built pure** — the three-layer model with `Ref`-typed
references, first-class geosets, unified curves, version-collapsed physics,
declarative population. `gltf_*.cpp` is then rewritten as a writer over it. The
expensive stage, and the one that must not be shortcut.
*Gate*: the 39 parse-tier test files must need no edits at all — if they do,
stages 1 and 3 have leaked into each other. Full suite green. Structural (not
byte-level) pre/post glTF comparison on the fixture set: same primitive / joint
/ clip / material inventory, every difference attributed.

**4. Bundle writer + manifest schema v1.**
*Gate*: new round-trip tests; the existing glTF conformance suite still green
(the projection must not regress).

**5. Blender addon** reading the manifest; delete every discovery mechanism I3
forbids.
*Gate*: a headless import test (`tests/blender_import_check.py`'s pattern)
proving zero arguments and zero husk dependency. The real interactive pass is
Luna's own — the same human gate `TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`
already carries.

**6. CLI regroup + structured output + corpus-task conversion.**
*Gate*: completions regenerated via `--print-completion` (never hand-edited);
converted tasks reproduce their last known corpus numbers
(`corpus_reports/corpus_scan_22_08/`).
