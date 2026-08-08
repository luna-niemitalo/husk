# TODO: WMO/ADT gameplay & misc metadata — final dispositions

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `../TODO_correctness.md`'s own convention) —
git history is the record of what was fixed and when, not this file.

Scope: `../../WORLD_COMPLETENESS.md`'s "Gameplay & misc metadata (not
independently renderable)" section — WMO per-face ground type
(`MOQG`/`MOGX`), WMO material/prepass overrides (`MDAL`/`MOPB`), WMO
unknown-structure chunk (`MOMX`), WMO rare/unclear chunk family (`MPB*`),
ADT sound emitter placement (`MCSE`), ADT chunk-level shadow map (`MCSH`),
ADT material override/blend batching (`MCMT`/`MCBB`). This file is a
companion to `../../WORLD_COMPLETENESS.md`, one level deeper for this specific
slice — same relationship prior `*_TODO.md` files had to `../../M2_COMPLETENESS.md`
before each was implemented and deleted in turn.

**This file's job is different from `COLLISION_CULLING_TODO.md`'s**: most
of `../../WORLD_COMPLETENESS.md`'s own text already calls these items `n/a`
(gameplay-only, no renderable shape) — this document's purpose is to give
each one an honest, *fully checked* final disposition, not to assume the
one-line dismissal is correct just because it sounds plausible. Several
items below turned out to need real correction once actually investigated
— this is the point of doing the investigation, not a failure of the
original survey. Every real-data claim below was checked this session
against the local corpus (`/media/luna/data/wow_export`, 84,798 real `.wmo`
files under `world/wmo`, 270,625 real `.adt` files) via independent Python
decoders (no husk code reuse — nothing in `src/` reads a WMO or ADT byte
yet).

## A real scanner bug, caught and corrected before any ADT number in this
## document was trusted

This session's first pass at an ADT chunk-tag census (`MCSE`/`MCSH`/`MCMT`/
`MCBB`) searched for each tag's **forward** spelling and found **zero**
hits across all 270,625 real `.adt` files — which would have been reported
as "confirmed absent," a wrong conclusion caught before it was written down
anywhere permanent. `../../DESIGN.md` already documents that ADT (like WMO)
reverses chunk tag bytes on disk, the opposite of M2's own inline chunks —
this session re-confirmed it directly against a real file (`azeroth_37_20
.adt`'s first 4 bytes are literally `52 45 56 4D` = `"REVM"`, not `"MVER"`)
before trusting any subsequent ADT byte-level claim. The first scan's
tag-matching logic never accounted for this and was silently searching for
a byte pattern that cannot occur in a real ADT file — every ADT-side number
in this document comes from a corrected, reversed-tag-aware rescan, not the
original broken one. Recorded here because it's exactly the kind of mistake
`../../DESIGN.md`'s own "Getting this backwards is a classic WMO/ADT-experience
trap" line warns about, and it's worth a future session not repeating it a
third time.

---

## 1. WMO per-face ground type (`MOQG`/`MOGX`)

**Current state**: `none`/`none`.

**Wiki citation**: `WMO.md`, `## MOQG chunk` and `## MOGX chunk` (both ≥
Dragonflight 10.0.0.46181).

**A real, worth-flagging correction to `../../WORLD_COMPLETENESS.md`'s own row,
found this session**: that row's text reads "`MOQG` (root)/`MOGX` (group)"
— **both chunks are actually group-level**, confirmed directly against
real bytes, not just re-read from the wiki's own ambiguous placement (the
wiki's group-file "always present" chunk-order list at the top of its "WMO
group file" section literally lists `MOQG` as the 10th and last
always-present chunk, and `MOGX` as the 2nd — but that same list is easy
to misread as describing only *some* of a root file's own chunks, since
neither heading explicitly restates "this is inside `MOGP`"). A full
84,798-file corpus census (seek-based, chunk-tag walk with explicit
recursion into `MOGP`'s own nested subchunk stream — see
`COLLISION_CULLING_TODO.md`'s own writeup of this exact recursion
requirement, discovered while building this session's scanning
infrastructure) found:

| Tag | Found at GROUP level (nested in `MOGP`) | Found at ROOT level (file top level) |
|---|---|---|
| `MOQG` | 2,219 / 71,929 group files | 0 / 12,869 root files |
| `MOGX` | 9,164 / 71,929 group files | not checked at root (not expected there per the wiki's own chunk-order list; 0 hits would be the prediction) |

Zero root-level `MOQG` hits across the entire corpus, only group-level —
this isn't a coin-flip result, it's a clean, unambiguous confirmation.
`../../WORLD_COMPLETENESS.md`'s row should read "`MOQG`/`MOGX` (both group)" —
flagged here for the consolidated documentation pass to correct (per this
task's own instructions, this file doesn't edit `../../WORLD_COMPLETENESS.md`
directly).

**Verified struct/field layout, `MOQG`**: `uint32_t queryFace[];` — one
`groundType` value per flagged polygon. **Confirmed correct against real
bytes**: decoded a real 6,760-byte `MOQG` chunk
(`dungeon/amirdrassilraid/10du_amirdrassilraid_fyrakkplatform01_000.wmo`,
1,690 entries) and a real 2,392-byte one
(`azeroth/buildings/stormwind/8sw_portalroom01_002.wmo`, 598 entries) —
both decode cleanly as flat `uint32_t` arrays, first 20 entries of each a
constant `10` (a single dominant ground-type value across most of a
building's queried faces, exactly the shape you'd expect for e.g. "stone
floor" applying to most of a room). No anomaly, no correction needed for
`MOQG` itself.

**A second, real correction found — `MOGX` is NOT "one single value"**:
the wiki's own text states `MOGX` is `uint32_t queryFaceStart;` — "Contains
one single value." Real files consistently disagree: **every real `MOGX`
chunk found this session (4 distinct real files, spanning an Argus
ship-wreckage piece, a Dragonflight-era dungeon, and a Stormwind building)
is exactly 256 bytes — 64 `uint32_t` slots, not 1.** Decoded directly:
only the *first* slot is ever non-zero (`23550` in one file, `35382`/`2` in
another — real, plausible `queryFaceStart` values), every remaining slot
is zero. This is the same "declared/allocated size vs. actually-used
content" shape `../../WIKI_FINDINGS.md` §11 already found for `DETL`'s own
16-byte chunk-alignment padding — a chunk reserved/padded out to a fixed
size where only the logically-relevant prefix carries real data. Proposed
correction: **`MOGX` is a fixed 256-byte (64×`uint32_t`) chunk, of which
only the first entry (`queryFaceStart`) is real/populated in every file
observed; the remaining 63 slots are reserved/always-zero padding**, not
a variable-length single-value chunk as the wiki states. This needs a wider
real-file sample before being fully confident the "63 always-zero slots"
half holds everywhere (4 files is a real but modest sample), but the
256-byte-not-4-byte size discrepancy itself is unambiguous across every
file checked.

**Real-data verification of the *concept* itself (not just the byte
layout)**: `MOQG`'s own values are gated behind `MPY2`'s `0x100` flag bit
(WMO_GEOMETRY_TODO.md's own scope for the materials half of `MPY2`) — this
session didn't decode a real `MPY2` chunk's flag bits to confirm the
`0x100` gate fires on real data (out of this file's own scope, since the
materials half of `MOPY`/`MPY2` belongs to the sibling geometry doc — see
`COLLISION_CULLING_TODO.md`'s own explicit ownership-split note for the
*collision*-relevant half of that same chunk).

**Consumption target, considered honestly rather than defaulted to `n/a`**:
per-face ground type drives footstep sounds and similar gameplay-only
audio/FX selection (per `../../WORLD_COMPLETENESS.md`'s own framing, and
consistent with `MOMT.ground_type`'s very similar, but *per-material* not
per-face, sibling field already documented on the same wiki page) — there
is no visual/geometric consequence to a ground-type value, and no existing
glTF/Blender convention treats "what does this floor sound like when
walked on" as renderable data. **Verdict, after real consideration, not a
reflexive default: `n/a` remains correct** — this is the one item in this
document where the original one-line dismissal holds up under scrutiny,
now for a stated reason rather than an assumption.

**Priority**: none for implementation (confirmed `n/a`) — but the `MOGX`
byte-layout correction above is worth recording in `../../WIKI_FINDINGS.md` once
a consolidated documentation pass happens, and the `../../WORLD_COMPLETENESS.md`
root/group mislabeling is worth fixing at the same time.

---

## 2. WMO material/prepass overrides (`MDAL`/`MOPB`)

**Current state**: `none`/`none`.

### 2a. `MDAL` — real, small, and probably worth `extras`, not `n/a`

**Wiki citation**: `WMO.md`, `## MDAL` (≥ Warlords of Draenor) —
`struct { CArgb replacement_for_header_color; } mdal;` — "if not present,
take color from header."

**Verified struct/field layout**: a single 4-byte `CArgb` (r,g,b,a bytes).
Confirmed group-level (not root) directly this session: the corpus census
found `MDAL` in **12,192 / 71,929 group files (17%)**, all nested inside
`MOGP`, zero at root level (0/12,869) — this resolves cleanly, no
ambiguity like `MOQG`/`MOGX` had.

**Real-data verification**: decoded 3 real `MDAL` chunks directly
(`argus/arguszone/7arg_argus_benodarplaceholder_000.wmo`,
`dungeon/amirdrassilraid/10du_amirdrassilraid_fyrakkplatform01_000.wmo`,
`azeroth/buildings/stormwind/8sw_portalroom01_002.wmo`) — all 3 decode to
`(255, 255, 255, 255)`, i.e. **full white/no-op** in every real sample
checked. This is a small sample (3 of 12,192 real hits) and it would be a
mistake to conclude the field is *always* a no-op from 3 data points — but
it's an honest, worth-recording observation: the mechanism may exist far
more often than it's actually used to *change* anything from the header's
own default color.

**Consumption target, reconsidered rather than defaulted to `n/a`**: unlike
`MOQG`'s ground type, `MDAL` genuinely **does** affect the rendered result
— it's a per-group override of the ambient/base lighting color used for
that group's vertex-lighting calculation (`MOHD.ambColor`'s per-group
substitute). A downstream renderer/Blender script that wants to reproduce
WMO's real in-game lighting tint would need this value. **Recommendation:
`extras`, not `n/a`** — attach as a per-group-mesh-node custom property
(e.g. `ambient_color_override: [r,g,b,a]`, only when the chunk is present
and differs from the header default, mirroring `--textures`'s existing
"quiet when nothing applies" policy) once WMO mesh export exists at all.
This is a real correction to `../../WORLD_COMPLETENESS.md`'s blanket `n/a` for
this row — flagged for the consolidated pass.

### 2b. `MOPB` — real, surprisingly common, genuinely opaque

**Wiki citation**: `WMO.md`, `## MOPB` (≥ Legion) —
`struct { char _1[0x18]; } map_object_prepass_batches[];` — the wiki gives
**zero field-level information**, not even a guess (contrast `MOMX`, where
the wiki at least ventures "most likely pointers to something").

**Real-data verification**: corpus census found `MOPB` in **35,368 /
71,929 group files (49%)** — group-level, confirmed absent at root
(0/12,869) — i.e. this is genuinely common in modern content, not a rare
edge case. Decoded a real record directly
(`arathi/8ara_arathirockwmo_04_000_lod1.wmo`, a single 24-byte record):
raw bytes `00000000 00000000 00000000 00ce0400 00750102 00`, which as 6×
`uint32_t` reads `(0, 0, 0, 0, 1230, 131445)` — the first three fields are
zero, the last two are real, non-trivial values. This is suggestive of a
"mostly reserved, populated fields near the end" shape (structurally the
mirror image of `MOMX`'s own "populated field first, zero padding after"
shape found in item 3 below) but **a single real record is not enough to
derive a struct from** — no attempt is made here to name these two fields,
per this project's own "don't invent a struct, record the honest unknown"
discipline for genuinely opaque wiki entries.

**Consumption target**: "prepass" strongly suggests a rendering-pipeline
optimization concept (Z/depth-prepass batch ordering, similar in spirit to
`MOBA`'s own render-batch role) rather than gameplay or visual-identity
data — i.e., even if fully decoded, this is very unlikely to encode
anything a static glTF export needs, since it's about *how* to draw
already-known geometry efficiently, not *what* to draw. **Recommendation:
stays `n/a`/diagnostic** (a raw per-record byte dump via a future
`dump-chunks`-equivalent, if anyone wants it, costs nothing) rather than
promoted to `extras` — unlike `MDAL`, there's no plausible visual-identity
argument for exposing this as glTF metadata even once decoded. Flagged
honestly as **investigated, partially decoded, disposition still `n/a` for
a stated reason** (not a lack of effort) — a genuine difference from
`MDAL`'s outcome despite both starting from "the wiki says nothing useful."

**Priority**: `MDAL`'s `extras` promotion is small and cheap once WMO group
export exists at all — bundle it into whatever session first builds real
WMO mesh output. `MOPB` needs no further work; its own opacity is a
property of the data, not of this investigation's effort.

---

## 3. WMO unknown-structure chunk (`MOMX`)

**Current state**: `none`/`none`. Per this task's own explicit instruction
("wiki itself says 'just a guess,' don't invent a struct, just record the
honest unknown") — but "record the honest unknown" turned out to mean a
real investigation was still worth doing, not a one-line shrug, and it
surfaced two genuinely new facts not on the wiki at all.

**Wiki citation**: `WMO.md`, `## MOMX` — root-level (appears before the
"# WMO group file" section header, alongside `MOPV`/`MOPT`/`MOPR`/`MOPE`/
`GFID`/`MDDI`/`MPVD` etc.): "Seems to be of 0x10 [size], the structure is
just a guess I observed that first value in the struct is always int16
dont know about the rest... `int16 data1..data8`. Observed in
`11du_arathormonastery_main01.wmo`."

**Correction #1, real and significant — this is not a rare, single-file
anomaly.** The wiki's phrasing ("Observed in [one named file]") reads as
"seen once" — a full corpus census found **`MOMX` in 3,931 of 12,869 real
root WMO files (30.5%)**, not a one-off. Both real files the local corpus
happens to have under the exact name the wiki cites
(`dungeon/arathormonastery/11du_arathormonastery_main01.wmo` **and**
`dungeon/arathordungeon/11du_arathormonastery_main01.wmo` — the same
named model exists in two different dungeon directories in this corpus)
were checked directly, plus a completely unrelated Argus zone file
(`argus/arguszone/7arg_kiljaedenship_brokenpiece01.wmo`) — all three carry
real `MOMX` data.

**Correction #2, a genuinely new structural fact not on the wiki at all:
`MOMX`'s record count exactly equals `MOHD.nTextures`, confirmed on all 3
files checked, zero exceptions:**

| File | `MOMX` size | Records (÷16) | `MOHD.nTextures` |
|---|---|---|---|
| `dungeon/arathormonastery/11du_arathormonastery_main01.wmo` | 1,168 bytes | 73 | 73 |
| `dungeon/arathordungeon/11du_arathormonastery_main01.wmo` | 816 bytes | 51 | 51 |
| `argus/arguszone/7arg_kiljaedenship_brokenpiece01.wmo` | 944 bytes | 59 | 59 |

The wiki's own stated 16-byte (`0x10`) stride is independently confirmed
(all 3 real sizes divide evenly by 16, and by no smaller stride
consistently across all 3 — e.g. 12-byte doesn't divide 944 or 1168
evenly). Combined with the exact `nTextures` match, the most defensible
real conclusion is: **`MOMX` is a per-texture (one record per `MOMT`/`MOTX`
texture slot) auxiliary table**, not tied to any other per-file count
(groups, doodads, lights, portals — none of those matched in any of the 3
files).

**Field-content investigation, honestly reported as unresolved rather than
forced into a clean answer**: within each 16-byte record, the two "arathor"
family files show a consistent shape — **the first 4 bytes (interpreted as
a little-endian `uint32_t`) carry a real, non-zero, repeated-within-file
value (observed: `7207`, `3350811`, `2299155`, or `0`), with the remaining
12 bytes always zero.** The Argus ship file shows the **opposite**
shape — the leading 4 bytes are zero in **every one of its 59 records**,
while bytes 4–11 (the middle 8 bytes) carry real, varied, non-zero
`int16` values that decode plausibly as a normalized 4-component
fixed-point vector (dividing by 32767 lands cleanly in [-1, 1], e.g.
`(-0.884, 0.913, 0.207, 0.233)` for record 0) — bytes 0–3 and 12–15 are
zero in every record of this file. **These two shapes are inconsistent
with a single simple "one populated `uint32_t` field" hypothesis** — they
look more like the wiki's own literal 8-independent-`int16`-fields guess
being correct after all, with different real files simply populating
different subsets of those 8 fields (field 0 in the arathor files vs.
fields 2–5 in the ship file) while leaving the rest at their default zero.

**Ruled out, checked directly rather than assumed**: the recurring
non-zero leading values in the arathor files (`7207`/`3350811`/`2299155`)
are **not** FileDataID references — cross-checked against both files' own
`GFID` (group-file FileDataIDs) and `MODI` (doodad FileDataIDs, Legion+)
arrays directly: zero matches in either file. This rules out "per-texture
FileDataID pointer" as the field's meaning, despite the wiki's own "most
likely pointers to something" hunch (which may still be right in a
different sense — e.g. a serialized in-memory pointer/handle from
whatever internal tool produced the file, not a meaningful on-disk
reference — but that's speculation, not a finding).

**Disposition**: genuinely partially investigated, not fully cracked —
real stride and real per-texture-count correlation are now established
facts (new information, not on the wiki), but per-field semantics remain
unresolved and this session's own 3-file sample actively shows two
different populated-byte-range shapes, which is itself informative (rules
out several simple hypotheses) without being enough to write a confident
canonical struct. A future session with a larger real sample (the full
corpus's 3,931 real hits, not just 3) — particularly checking whether the
populated-field-position correlates with WMO version, expansion era, or
some other per-file property — is the natural next step, not guessed at
here.

**Consumption target**: `n/a`/diagnostic (a raw per-texture-slot record
dump, if anyone wants it) — no evidence surfaced this session suggests a
renderable concept; keep it firmly in "record the honest unknown," now
with real per-corpus prevalence and structural bounds attached rather than
a bare shrug.

**Priority**: low for implementation, but the two corrections above
(prevalence, `nTextures` correlation) are worth a `../../WIKI_FINDINGS.md` entry
in the consolidated pass regardless of whether `MOMX` itself is ever
ingested by husk.

---

## 4. WMO rare/unclear chunk family (`MPBV`/`MPBP`/`MPBI`/`MPBG`)

**Current state**: `none`/`none`. Confirmed, not merely repeated.

**Wiki citation**: `WMO.md`, `## MPB*` — "These chunks are barely ever
present (the one file known is `StonetalonWheelPlatform.wmo` from alpha).
No version of the client ever read them though." Struct given in full
(a block/polygon/index/vertex hierarchy reconstructing small
platform/plank/rail-shaped debug geometry).

**Real-data verification**: the full 84,798-file corpus census (seek-based,
recursing correctly into every group file's `MOGP` payload — the same
infrastructure item 1's `MOQG`/`MOGX` resolution used) found **`MPBV`/
`MPBP`/`MPBI`/`MPBG`: 0 / 71,929 group files, all four, zero exceptions.**
This is a genuine, corpus-wide confirmation of the wiki's own rarity claim
(not just "not in my small sample" — the full real corpus this session had
access to), consistent with these being a single-alpha-file, pre-1.0
leftover never touched by any shipped client and (per this specific
corpus, which appears to skew modern/retail-era) not present anywhere in
current game data either.

**Disposition**: `n/a`, confirmed with the strongest possible real-data
backing available this session (full corpus, not a sample) — nothing to
implement, nothing to watch for. This is the cleanest, most
open-and-shut item in this document.

**Priority**: none.

---

## 5. ADT sound emitter placement (`MCSE`)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/ADT/v18.md`, `### MCSE
sub-chunk` — modern (retail-era) struct, 28 (0x1C) bytes:
`struct CWSoundEmitter { foreign_key<uint32_t, SoundEntriesAdvancedRec> entry_id; C3Vector position; C3Vector size; };`
(older 0.5.3/1.12.1-era structs are much larger and pre-date this
project's scope, per README's own version-floor conventions elsewhere).
`split files: root` — lives in the plain/root ADT file (or the pre-Cata
monolithic file), not `_tex0`/`_obj0`.

**Verified struct/field layout**: confirmed directly — `entry_id`
(`uint32_t`), `position` (`C3Vector`, 12 bytes), `size` (`C3Vector`, 12
bytes) = 28 bytes exactly, matching the wiki's own stated "WoW takes only
0x1C bytes per entry" correction over an older, longer struct.

**Real-data verification, and a real nuance worth recording**: `MCSE` as a
**chunk tag** is present essentially everywhere real terrain is (found in
all 256 `MCNK`s of both real Azeroth-continent tiles checked directly), but
in every real tile sampled this session (2 tiles checked exhaustively,
plus a 400-file random sample across `world/maps` checked for
`nSndEmitters > 0` specifically), **`MCNK.nSndEmitters` was 0 and the
`MCSE` chunk itself was present-but-empty (chunk size 0) in every single
case** — i.e. the mechanism is real and structurally ubiquitous, but this
session did not find one real file with an actual non-zero ambient sound
emitter placed. This is an honest, real negative result, not a parsing
gap — the chunk header itself decodes correctly (found at the expected
offset, `ofsSndEmitters`/`sizeSndEmitters` both present and internally
consistent with size 0) and the code that finds it also found `MCSH` real
data over 500 bytes in other files without trouble (see item 6), so the
absence of populated `MCSE` records isn't a tooling artifact as far as this
session could tell. Possible explanations not confirmed either way: modern
retail ambient sound may route primarily through a different mechanism
(area triggers, zone-wide ambience tables) with `MCSE` itself being a
increasingly-vestigial per-tile placement mechanism; or this specific
local corpus's own extraction/build happens to undersample tiles that use
it. A future session with more real hits (a targeted, larger sample
specifically hunting for `nSndEmitters > 0` — this session's own 400-file
sample was not targeted at all, just uniform-random) would settle this.

**Consumption target, reconsidered rather than defaulted to `n/a` per
explicit direction**: `MCSE`'s `position` field is a real, concrete 3D
point — structurally the same "id + position" shape M2's own
`M2Ribbon`/`M2Particle` emitters already get a real glTF `extras` anchor
for (`gltf::Skeleton::EmitterAnchor`, `../../M2_COMPLETENESS.md`'s Interaction
points & effects section). There is no reason a sound emitter's position
can't get the exact same treatment once ADT terrain export exists: a
minimal marker node (or a `sound_emitters` array in the terrain tile's own
`extras`, mirroring the M2 pattern's `ribbon_emitters`/`particle_emitters`
naming) carrying `entry_id`/`position`/`size` per real emitter — audio
playback itself is out of scope (glTF has no audio-emitter concept husk
would target), but *placement* is exactly as renderable/inspectable as any
other point-in-space husk already exports this way. **Recommendation:
`extras`, matching the ribbon/particle precedent exactly — not `n/a`.**
This is a real correction to `../../WORLD_COMPLETENESS.md`'s blanket `n/a` for
this row.

**Priority**: low-to-medium — real, cheap once ADT parsing infrastructure
exists (a flat per-`MCNK` record read, no cross-references to validate),
directly reuses an existing husk design pattern rather than inventing a
new one. Blocked only on real example data existing somewhere in the
corpus to test against — worth a more targeted search before implementing.

---

## 6. ADT chunk-level shadow map (`MCSH`)

**Current state**: `none`/`none`. **Reconsidered under explicit direction**
to weigh this the same way `WMO_GEOMETRY_TODO.md` treats WMO's own `MOCV`/
`MOC2` (baked per-vertex lighting) — i.e. baked-lighting/shadow data is a
real, exportable concept once *some* form of terrain mesh/UV export
exists, not something to wave away just because it's raster rather than a
discrete point.

**Wiki citation**: `ADT/v18.md`, `### MCSH sub-chunk` — `split files: tex`
(lives in the Cata+ `_tex0` split file, or the monolithic pre-Cata file):
`struct { uint1_t shadow_map[64][64]; } mcsh;` — a 1-bit-per-texel bitmap,
512 bytes, LSB-first per row, "or 63x63 with the last column/row/cell
auto-filled" depending on the `do_not_fix_alpha_map` flag (shared with
`MCAL`'s own alpha-map fixup behavior).

**Verified struct/field layout, real bytes**: found and decoded **every
real `MCSH` hit as exactly 512 bytes**, zero exceptions across every file
checked — matches `64×64÷8 = 512` exactly, confirming the wiki's stated
size precisely.

**A real scanner-location lesson, worth recording**: this session's first
few targeted checks (plain/root `azeroth_*.adt` files, and the pre-Cata
`_obj0`-adjacent monolithic file `6846952-6847091_14_1.adt`) all found
`MCNK.flags.has_mcsh == 0` and zero real `MCSH` chunks — which briefly
looked like "genuinely absent in this corpus," until a random 3,000-file
sample search (raw reversed-tag byte search across the *whole* corpus, not
scoped to any one directory) found **47 real hits, and every single one
was a `_tex0.adt` file** — exactly matching the wiki's own "split files:
tex" annotation. The lesson: `MCSH` (like `MCMT` below) genuinely does live
in the Cata+ split `_tex0` file specifically, not the plain/root file — a
scan (or a future implementation) that only reads root ADT files will
never find it, even though it's real and present elsewhere in the same
logical tile. Confirmed directly by decoding
`world/maps/2454/2454_55_8_tex0.adt`: its `MCNK` sub-chunks in this
split-file variant have **no 128-byte header at all** (per the wiki's own
"split files: header in root, no header in obj and tex" line, now verified
byte-for-byte) — sub-chunks (`MCLY`, `MCSH`, `MCAL`) start at the `MCNK`
payload's own byte 0, a structurally different layout from the monolithic/
root-file case this session had to discover by direct decode rather than
assume from the header struct listing alone.

**Real-data verification, and the strongest single piece of evidence in
this document that the concept is genuinely translatable**: decoded every
real `MCSH` bitmap in `2454_55_8_tex0.adt` (36 of 256 `MCNK`s carry one) and
computed each one's real bit density (fraction of "shadowed" texels).
**The result is strongly bimodal — almost every chunk's density sits near
0.0 (essentially unshadowed) or near 1.0 (essentially fully shadowed)**,
e.g. `0.99, 0.009, 0.996, 0.011, 0.991, 0.007, 1.0, 0.078, 1.0, 0.999,
0.015, 1.0, 1.0, 0.016, 1.0` for the first 15 populated chunks, average
`0.613` across all 36. This is exactly the shape a real baked static-
occluder shadow mask should have (most terrain chunks are either entirely
in a large object's shadow or entirely not, with only chunks actually
straddling a shadow boundary landing in between) — strong, real evidence
this is genuine baked lighting data decoding correctly, not noise or a
misaligned read.

**Consumption target, reconsidered per explicit direction, with the
resolution-mismatch reasoning stated explicitly rather than hand-waved**:
`MCSH`'s 64×64 texel resolution does **not** map cleanly onto a per-vertex
channel the way WMO's `MOCV` does — `MCVT`'s own heightmap grid is only
9×9+8×8 = 145 vertices per `MCNK`, a genuine resolution mismatch (64×64 =
4,096 texels vs. 145 vertices, a ~28x difference) that would force a lossy
downsample if forced into a vertex-color channel, unlike `MOCV` (which is
already stored per-vertex in WMO, no mismatch to reconcile). **The natural,
non-lossy translation is a small per-`MCNK` texture** (e.g. an R8 grayscale
512-byte bitmap unpacked to a 64×64 image, applied as a multiply/shadow
layer over the terrain's base color) — structurally the same shape
`MCAL`'s own alpha maps already need for texture-layer blending (also a
64×64 raster, also documented in the same `v18.md` page, presumably
already in `WMO_GEOMETRY_TODO.md`'s ADT-terrain-texturing sibling scope).
**Recommendation: `extras`/diagnostic now (a raw bitmap dump, real and
decodable today), `native-possible, not done` as the ceiling** once ADT
terrain UV/texturing export exists at all — mirroring `MOCV`'s own
`native-possible` framing in the geometry doc, not `n/a`. This is a real
correction to `../../WORLD_COMPLETENESS.md`'s blanket `n/a` for this row, made
only after confirming (a) the bitmap decodes to real, non-garbage data and
(b) a concrete, honest reason (resolution mismatch, not "no position to
anchor it to") for why the *texture* shape is right and the *vertex-color*
shape isn't.

**Priority**: low-to-medium, same tier as `MCSE` — real, decodable today,
blocked mainly on ADT terrain mesh/UV export existing at all before a
texture-shaped consumption target means anything. Worth flagging to
whoever implements ADT terrain texturing that `_tex0`/`_obj0` split files
need to be read for this (and `MCMT`, below) even if the base terrain mesh
itself comes entirely from the root file's own `MCVT`/`MCNR`.

---

## 7. ADT material override / blend batching (`MCMT`/`MCBB`)

**Current state**: `none`/`none`.

### 7a. `MCMT` (Cata+) — real, render-relevant, reconsider ceiling

**Wiki citation**: `ADT/v18.md`, `### MCMT (Cata+)` — `split files: tex` —
`struct { foreign_key<uint8_t, TerrainMaterialRec> material_id[4]; } MCMT;`
— one material-ID override per `MCLY` texture layer (up to 4).

**Real-data verification**: found and decoded a real 4-byte `MCMT` chunk
(`world/maps/azeroth/azeroth_42_16_tex0.adt`) — raw bytes `f2 00 00 00`,
i.e. `material_id[0] = 0xF2 = 242`, the remaining three layer slots `0`.
Real, plausible `TerrainMaterialRec` foreign-key value (not zero/garbage),
found in a `_tex0` split file exactly per the wiki's own annotation — same
split-file-location lesson item 6 already established.

**Consumption target, reconsidered**: this is **not** gameplay-only —
`TerrainMaterialRec` per the wiki's own foreign-key annotation drives real
shader/material selection for that texture layer (parallel to how WMO's
own `MOMT.shader` index selects a real rendering shader) — a downstream
renderer wanting visually-correct terrain material behavior (PBR-ish
surface response, not just the base color texture) would want this value.
**Recommendation: `extras` on the terrain mesh's per-layer material data**
(once ADT terrain materials/texturing export exists at all), not `n/a` —
another real correction to `../../WORLD_COMPLETENESS.md`'s blanket dismissal for
this row.

### 7b. `MCBB` (MoP+) — real presence not confirmed this session, kept `n/a`-leaning

**Wiki citation**: `ADT/v18.md`, `### MCBB (MoP+)` — `split files: root,
lod` — a blend-batch index table (`mbmh_index`/`indexCount`/`indexFirst`/
`vertexCount`/`vertexFirst`, referencing further `MBMH`/`MBMI`/`MBNV`
chunks this session did not investigate at all) — "blend batches, max 256
per MCNK."

**Real-data verification**: **not found** in a 300-file random sample of
`_tex0.adt` files this session checked (0/300) — but per the wiki's own
"split files: root, lod" annotation, `_tex0` was almost certainly the
wrong place to look for this one (unlike `MCSH`/`MCMT`, both explicitly
`tex`-scoped) — this session's negative result is **not** strong evidence
of corpus-wide absence, just evidence that the wrong file type was
sampled. A real follow-up should check root/`_lod` files specifically
before concluding anything.

**Consumption target**: unresolved — the referenced `MBMH`/`MBMI`/`MBNV`
chunk family (blend-mesh vertices/indices) sounds structurally like it
could be genuine extra terrain-decoration geometry (matching the "blend
mesh" name — likely a mechanism for blending a secondary decorative mesh
into the terrain surface, e.g. rubble/rock transitions), which would make
it `native-possible` rather than `n/a` if confirmed — but this session
did not chase the `MBMH`/`MBMI`/`MBNV` structs down, so this is flagged as
a **genuinely open question for a future session**, not a considered
`n/a` the way `MOQG`/`MOPB` are. Recommend: check root/`_lod` ADT files
specifically for real `MCBB`/`MBMH`/`MBMI`/`MBNV` hits before writing this
one off.

**Priority**: `MCMT` — low-to-medium, same tier/blocking-condition as
`MCSH` above (real, decodable, needs ADT terrain materials pipeline to be
worth anything). `MCBB` — needs a real follow-up scan (root/`_lod` files,
not `_tex0`) before its priority can even be assessed honestly.

---

## Summary table (final dispositions)

| Item | Real corpus presence this session | Wiki correction found | Final ceiling |
|---|---|---|---|
| `MOQG` | 2,219/71,929 group files | root/group mislabel in `../../WORLD_COMPLETENESS.md` | `n/a` (confirmed, reasoned) |
| `MOGX` | 9,164/71,929 group files | **real: 256 bytes, not "one single value"** | `n/a` (ground type itself; byte-layout correction still worth recording) |
| `MDAL` | 12,192/71,929 group files | none to struct; consumption reconsidered | **`extras`** (was `n/a`) |
| `MOPB` | 35,368/71,929 group files (49%!) | none (wiki gives nothing to correct) | `n/a`, investigated honestly |
| `MOMX` | 3,931/12,869 root files (30.5%, **not rare**) | **real: record count == `nTextures`**, field semantics partially resolved | `n/a`/diagnostic, real unknowns recorded |
| `MPBV`/`MPBP`/`MPBI`/`MPBG` | 0/71,929 group files (full corpus) | none — wiki's rarity claim fully confirmed | `n/a`, closed |
| `MCSE` | chunk ubiquitous, real non-empty data not found this session | none to struct | **`extras`** (was `n/a`) |
| `MCSH` | 47/3,000 sampled files (all `_tex0`), bimodal real shadow data confirmed | split-file-location lesson (not `root`) | **`extras`/`native-possible, not done`** (was `n/a`) |
| `MCMT` | confirmed real in `_tex0` files | split-file-location lesson | **`extras`** (was `n/a`) |
| `MCBB` | not confirmed (wrong split-file type sampled) | none yet — open | unresolved, needs follow-up scan |

Four of ten items were promoted out of a blanket `n/a` this session
(`MDAL`, `MCSE`, `MCSH`, `MCMT`) — each for a stated, evidence-backed
reason, not a reflexive "more coverage is always better." Three items
(`MOQG`, `MOPB`, `MPB*`) were investigated and confirmed to genuinely
belong at `n/a`/closed. `MOMX` got substantial new structural findings
without a full semantic crack. `MCBB` is the one honestly-unresolved item,
flagged for a real follow-up rather than guessed at.

---

## Test plan

- **Synthetic fixtures**: not warranted for any item promoted to `extras`
  in this document until the underlying WMO group-mesh / ADT terrain-mesh
  export they'd attach to exists at all — these are metadata-on-existing-
  geometry items, not standalone parsers needing their own synthetic
  byte-blob tests the way `.phys`/`.bone` did.
- **Real fixture candidates**, already confirmed present this session:
  - `MDAL`: `world/wmo/argus/arguszone/7arg_argus_benodarplaceholder_000.wmo`
    (small, 4-byte chunk, real value `(255,255,255,255)`).
  - `MOMX`: `world/wmo/dungeon/arathordungeon/11du_arathormonastery_main01.wmo`
    (smallest of the three checked, 51 records, `nTextures`-matching).
  - `MCSE`/`MCSH`/`MCMT`: `world/maps/2454/2454_55_8_tex0.adt` (`MCSH`
    confirmed with 36 real populated bitmaps) paired with
    `world/maps/2454/2454_55_8.adt` (the same tile's root file, for
    `MCSE`'s own root-file-scoped data — though this session did not find
    non-empty `MCSE` records in this specific tile either; a wider search
    is needed before a real `MCSE` fixture can be named with confidence).
  - `world/maps/azeroth/azeroth_42_16_tex0.adt` for a real `MCMT` hit.
- **Corpus-wide follow-up work flagged for a future session, not done
  here**: (a) a targeted (not uniform-random) search for a real ADT tile
  with `nSndEmitters > 0`; (b) `MCBB`/`MBMH`/`MBMI`/`MBNV` checked against
  root/`_lod` ADT files specifically, not `_tex0`; (c) a wider `MOMX`
  sample (the full 3,931 real hits, not 3) to test whether populated-
  field-position correlates with any per-file property.

## References

- **wowdev.wiki** (`documentation/wowdev-wiki/md/`): `WMO.md` (`MOQG`,
  `MOGX`, `MDAL`, `MOPB`, `MOMX`, `MPB*`, `MOHD`, `GFID`, `MODI`, group-file
  chunk-order list); `ADT/v18.md` (`MCNK` header struct incl. `has_mcsh`/
  `ofsSndEmitters`/`nSndEmitters` fields, `MCSE`, `MCSH`, `MCMT`, `MCBB`,
  `MCAL`'s shared alpha-map-fixup framing). No `HUSK_AMENDMENTS.md` entry
  for any WMO/ADT page exists yet (expected — nothing WMO/ADT-side
  implemented before this session's sibling investigations).
- **husk `src/`**: `src/m2.hpp`/`gltf.hpp` (`EmitterAnchor`, the
  id/bone/position minimal-anchor pattern `MCSE`'s own recommendation
  mirrors directly); no ADT/WMO parsing code exists yet to reuse beyond
  this pattern.
- **`../../M2_COMPLETENESS.md`**: Interaction points & effects section (the
  ribbon/particle `extras` precedent `MCSE` reuses).
- **`COLLISION_CULLING_TODO.md`** (sibling document, same session): the
  `MOGP`-recursion requirement that both documents' corpus scans needed;
  the `MOQG`/`MOGX` root-vs-group resolution was actually done as part of
  that document's own scanning infrastructure and reported here since it's
  this document's own item.
- **Real corpus**: `/media/luna/data/wow_export/world/wmo` (84,798 real
  `.wmo` files, full-corpus seek-based census for every WMO-side item in
  this document — exact counts throughout); `/media/luna/data/wow_export`
  (270,625 real `.adt` files; a corrected, reversed-tag-aware full-corpus
  scan was run for `MCSE`/`MCSH`/`MCMT`/`MCBB` — see the scanner-bug note
  at this document's top — plus multiple smaller targeted/random samples
  for real-byte decoding, exact paths named per item above). All scanning
  scripts were independent, from-scratch Python (not committed, per this
  task's own scratch-script policy), no husk code reuse.
