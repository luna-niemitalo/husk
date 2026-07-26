# TODO: correctness &amp; usability gaps

**Status: Item 3 needs work, rest are left for later version** (each
with regression tests — `tests/test_m2.cpp`, `test_gltf.cpp`, `test_cli.cpp`).
Items 1, 2, and 4 remain open need real-file reverse-engineering
work this pass deliberately didn't attempt
 5 is low-priority by design, not by oversight. Left in place as a record of what
was found and fixed, not as a stale list.

---

## Read-pipeline correctness

### 1. `.skel`-sourced external `.anim` files (`AFSB`) are unparsed

**Still open — deliberately not attempted this pass.** All 54/54 sampled
`.anim` files for the `.skel`-sourced HD test model carry `AFSB` instead of
usable `AFM2` data. For any Legion+ character whose bones live in a `.skel`
file (the norm for modern character models), external-sequence animation is
essentially 0% available. `AFSB`'s own byte layout is undocumented anywhere
(see `WIKI_FINDINGS.md` §2) — a genuine reverse-engineering task, not a
quick parser fix.

---

### 2. Particles (`M2Particle`) / ribbons (`M2Ribbon`)

**Ribbons: fixed** (see below). **Particles: still open.**
`M2Particle` is a large, heavily version-conditional struct (BC/Cata+
branches for several fields) — enough real-file investigation to get right
is out of scope for this pass. Currently count/offset-only in `husk info`.
Still core to WoW's visual identity (weapon trails, magic, fire/smoke) and
0% implemented.

**`M2Ribbon`: fixed.** `m2::Ribbon`/`m2::parseRibbons` (`src/m2.cpp`,
0xB0-byte record confirmed against the wowdev.wiki mirror) surfaces the
static fields — `ribbonId`/`boneIndex`/`position`/`edgesPerSecond`/
`edgeLifetime`/`gravity`/`textureRows`/`textureCols` — same
static-fields-only pattern as Attachment/Event/Light; the six embedded
`M2Track`s (color/alpha/height/texSlot/visibility) and the two
`M2Array<uint16_t>` lookup tables are left unread, consistent with that
pattern. Surfaced via `husk info`.

---

### 3. shell-completion script

**No `husk` shell-completion script** (bash/zsh/fish). Given CLI.md
  §2.3 treats autocomplete as "the interface, not an add-on," and husk's
  own subcommand/flag surface is small and stable, a completion script
  would be cheap and would directly serve the doc's discovery-by-doing
  ideal (flag names, and — where feasible — `--lod`'s `all` literal and the
  `auto` keyword). Not urgent for a single-developer local tool, but worth
  a mention since it's rung 2 of CLI.md's own fallback chain and currently
  entirely absent at every subcommand level.
---

### 4. [Fixed PARTIAL] `.phys` / `PFID`

**Fixed:** `Header::physFileId`, wired through `resolveBlob` exactly like
`SKID`/`skeletonFileId` (confirmed `PFID` is a single `uint32_t`, same
shape as `SKID`, via the wowdev.wiki mirror — not an array like
`BFID`/`AFID`). Surfaced via `husk info`. Actual `.phys` file *content*
parsing is still unstarted — this only closes the "not even surfaced"
gap, matching where `SKID` was before `.skel` support existed.

---

### 5. Cameras (`M2Camera`) — low priority, explicitly deprioritized

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

