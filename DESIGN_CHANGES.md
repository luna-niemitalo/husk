# DESIGN_CHANGES.md — CLI usability pass

Tracking doc for one work session's worth of `README.md`/`DESIGN.md` changes,
written so a separate task can verify the codebase actually conforms to what's
decided here. Two parts: what's **done** (README Usage rewrite — verify it
stays accurate as the code changes below land) and what's **planned**
(`export`'s CLI-grammar migration to CLI11 — not implemented yet; this is the
spec + checklist for that work). Full rationale for the planned part lives in
`DESIGN.md`'s "CLI argument grammar for `export`" section — this file is the
actionable summary + checklist derived from it, not a replacement. Once the
planned part ships, fold anything still true here back into `DESIGN.md`/
`README.md` and delete this file — it's a migration-tracking scratch doc, not
a permanent layer alongside `DESIGN.md`/`FAILURES.md`/`WIKI_FINDINGS.md`.

---

## Part 1 — DONE: README `Usage` section rewrite

**What changed**: `README.md`'s `## Usage` section was a single ~300-line
undifferentiated prose block mixing actual usage docs with a ~150-line
development-verification narrative (bloodelffemale.m2 byte-counts, bug
discovery stories, regression-test pointers). Rewritten per
`~/docs/READABILITY.md`/`~/docs/CLI.md`:

- Split into one `###` subsection per subcommand (`husk info`, `husk export`,
  `husk dump-chunks`, texture conversion) instead of one wall of paragraphs —
  `CLI.md` §2.1, structure over memorization.
- `export`'s flags pulled into a table (flag / meaning / default) instead of
  buried mid-paragraph.
- The verification narrative was cut, not reorganized — it duplicated
  `FAILURES.md`/`FAILURES2.md` almost fact-for-fact (checked via grep before
  cutting); a one-line pointer to those files replaced it. `README.md` is the
  "how to use it" layer per `READABILITY.md` §5, not the exhaustive-history
  layer.
- Short, realistic invocation examples added per subcommand in place of the
  single giant worked example.

**Real correctness bugs this surfaced and fixed** (found by checking the
rewritten prose against `src/cmd_export.cpp` directly, not by trusting the
old README text):

- The old text said "husk doesn't resolve the `.skin`/`.skel` filenames
  itself." **False** — `src/cmd_export.cpp` (`findSameBasenameSkins`, and the
  block defaulting `texturesDir`/`skinDir`/`animDir` to the model's own
  directory) already auto-resolves `.skin`/`.skel`/`--textures`/`--skin-dir`/
  `--anim-dir` by filesystem convention when omitted, announced on stderr.
  What husk actually never does is resolve a FileDataID to the *real* WoW
  filename via CASC/listfile — a narrower, correct claim. README now
  documents the real default-resolution behavior under `export`'s
  "**Defaults**" bullet list.
- The old text implied `--skin-dir` was "required alongside `auto`." **False**
  — it defaults to the model's own directory exactly like `--textures`/
  `--anim-dir`. Fixed in the flags table.

**Status**: matches actual current (`main`-branch, pre-CLI11-migration)
`cmd_export.cpp` behavior as of this session. Once Part 2 lands, this section
needs a second pass — the flag names, defaults, and grammar shape all change.

---

## Part 2 — PLANNED: `export`'s CLI grammar → named flags via CLI11

**Not implemented.** Full derivation: `DESIGN.md`, "CLI argument grammar for
`export`" section. Summary below is the spec to build and test against.

### Why

Current `export` grammar mixes up to three trailing-optional *positionals*
(`.skin`|`auto`, output `.glb`, `.skel`) with flags layered on top — a
token's meaning depends on how many words preceded it, which is `CLI.md`
§2.2's "no shared grammar" failure mode, and is why `--help` needed a
dedicated pre-parse special case instead of just working.

### Library

[CLI11](https://github.com/CLIUtils/CLI11) — header-only, MIT. Chosen over
hand-rolling a bigger parser or a Boost-style everything-framework, per the
project's tight-fit-library policy (`tinygltf`-style: one job, done well).
**Requires adding a new dependency to `nix/flake.nix` — needs explicit
sign-off before that edit lands**, per this project's package-approval rule.

### Target grammar

| Flag | Short | Values | Default |
|---|---|---|---|
| `--input` | `-i` | `.m2` path | required; 1st positional if flag omitted |
| `--output` | `-o` | output `.glb` path | `<model-basename>.glb`; last positional if flag omitted |
| `--skin` | `-s` | a `.skin` path, or `auto` | `auto` |
| `--textures` | `-t` | directory of `<FileDataID>.png` | model's own directory |
| `--skin-dir` | *(none)* | directory of `<FileDataID>.skin` | model's own directory |
| `--anim` | `-a` | directory, or `auto`/`inline`/`none` | `auto` |
| `--skel` | *(none)* | external `.skel` path | same-basename `.skel` next to model, if any |
| `--lod` | *(none)* | `<n>` or `all` | entry `0`; only meaningful when `--skin` resolves via `auto` |

All flags order-independent. `-i`/`-o` are the only ones with a positional
fallback (universal `tool in out` muscle memory — `CLI.md` §1); every other
flag is named-only, so a tenth flag added later can never shift what
positional word #3 means.

### Per-flag state machine (this is the part a conformance task must check
behaviorally, not just by flag presence)

**`--textures` / `--skel` / `--skin-dir`** — plain three-state (`CLI.md`
§2.11): unset → `auto` (best-effort derivation; the *directory* defaults to
the model's own directory as one current heuristic, not as the definition of
`auto` itself); explicit value → override; explicit `none` → deliberately
skip, never attempted, never warned about.

- `--textures none`: never embed an image even if one would resolve.
- `--skel none`: never look for a same-basename `.skel`, even if one exists —
  forces an unskinned mesh regardless of inline-bone count.
- `--skin-dir none`: `--skin auto` skips the `SFID`-FileDataID search stage
  entirely, falls straight to the same-basename numbered scan.

**`--skin`** — deliberately *not* three-state. `auto` (default) folds what
are currently two separate code paths into one: try the `SFID`-declared
FileDataID match first (in the resolved `--skin-dir`), then the
same-basename numbered scan (`findSameBasenameSkins`) as fallback. Order
matters and is decided: FileDataID match first (the model's own
self-description) beats the filesystem heuristic. **`--skin none` is
rejected at parse time** — a `.skin` is the sole source of triangle/submesh/
batch data (`skin::resolveTriangleIndices`/`parseSubmeshes`/`parseBatches`),
not optional enrichment, so `none` isn't a real state for it. (A genuine
skin-less/point-cloud export mode was explicitly considered and rejected as
out of scope for this change — if ever wanted, it needs its own design
discussion, not a quiet `none` case here.)

**`--anim`** — four states, not three, because it bundles two independent
axes (whether any animation exists at all vs. whether *external* files
contribute):

- `auto` (default): inline sequences + global-sequence bone tracks (both
  resolved straight from the model's own blob, no filesystem involved) *plus*
  best-effort external-directory search.
- `inline`: inline sequences + global-sequence tracks only; external-
  directory search explicitly skipped.
- `none`: **new behavior, not a rename** — zero `animation` clips at all,
  inline or external. The bind pose (`JOINTS_0`/`WEIGHTS_0`, inverse bind
  matrices) is untouched; only `animation` clips are suppressed. Nothing in
  today's code has an early-out for this case yet.
- `<dir>`: explicit override for the external-search directory; inline +
  global-sequence tracks still resolve on top of it, same as `auto`.

### `inline` was checked against every other flag, and deliberately not added elsewhere

General rule (full derivation: `DESIGN.md`'s "Does `inline` generalize past
`--anim`?"): a flag earns a fourth `inline` state only when (1) the model's
own blob independently produces a non-empty result with zero filesystem
access, **and** (2) that inline result and an external one aren't mutually
exclusive — both can be true at once and the export combines them. `--anim`
is the only flag meeting both. Checked and rejected for the others:

- **`--skel`**: fails condition 2. Inline bones and an external `.skel` are
  mutually exclusive in every real file examined and in this project's own
  resolution order (external `.skel` is only ever consulted when inline
  bones are empty) — a `--skel inline` state would be indistinguishable from
  `auto` or from `none` in every case, never independently meaningful.
- **`--textures`**: fails condition 1. WoW never embeds pixel data in the
  M2/`.skin` itself (every texture is BLP-external by construction) — there
  is no inline image to fall back to.
- **`--lod`**: not this shape of flag at all — an index selector into
  already-resolved `SFID` entries, not a resolution-source flag.
- **Other baked-in `extras`** (geoset ID/group/variant, second-texture-layer
  metadata, texture-transform data): not gated by any resolution flag,
  inline or otherwise — always read unconditionally from the model's own
  data, nothing to extend.

A conformance task should treat the **absence** of `--skel inline`/
`--textures inline`/similar as correct, not a gap — adding them would be a
regression against this reasoning, not missing coverage.

### Files that need to change together (this is a breaking change to every
existing `husk export` invocation's argument order — not additive)

- `nix/flake.nix` — add CLI11 (needs sign-off first).
- `src/cmd_export.cpp` — replace the hand-rolled positional/flag parsing
  block and `printUsage` with CLI11; implement the `--skin auto` two-stage
  fallback; implement `--anim`'s four real states (today there's only a
  boolean-ish "is `--anim-dir` given" check); reject `--skin none` at parse
  time.
- `README.md` — `export`'s Usage subsection needs a second pass once the
  flags above are real: synopsis, defaults list, flags table, examples.
- `tests/test_cli.cpp` — the current positional-ordering test cases test a
  grammar that will no longer exist; needs new cases per state below.

### Verification checklist

- [ ] `husk export --help` lists every flag above with its short form where
      specified, and works regardless of what else is on the command line.
- [ ] `-i`/`-o` still work as bare positionals (first/last) when their flags
      are omitted; every other argument is rejected as a bare positional.
- [ ] All flags accepted in any order relative to each other.
- [ ] `--skin` omitted behaves identically to `--skin auto` explicitly.
- [ ] `--skin auto`: a model whose `SFID`-declared FileDataID exists in
      `--skin-dir` uses that file, *not* the same-basename scan result, when
      both would match (proves ordering, not just "some" resolution).
- [ ] `--skin auto`: when no `SFID`-declared FileDataID file exists but a
      same-basename `<N>.skin` does, that's used (fallback path exercised).
- [ ] `--skin none` is rejected with a clear error naming the actual expected
      values (a path, or `auto`) — never silently accepted.
- [ ] `--skin-dir none`: with `--skin auto`, only the same-basename scan
      runs, even if a matching FileDataID-named file also exists in the
      model's directory.
- [ ] `--textures none`: no image embedded even when a matching
      `<FileDataID>.png` sits in the default directory.
- [ ] `--skel none`: unskinned mesh even when a same-basename `.skel` exists
      next to the model.
- [ ] `--anim auto` (or omitted): output identical to today's default
      behavior (regression baseline — same clip count against the existing
      `bloodelffemale.m2` fixtures).
- [ ] `--anim inline`: clip count equals inline + global-sequence clips only,
      even when a matching external `.anim` directory is available.
- [ ] `--anim none`: zero `animation` clips in the output `.glb`, but
      `JOINTS_0`/`WEIGHTS_0`/inverse-bind-matrices still present if the model
      has bones.
- [ ] `--anim <dir>`: external clips resolve from exactly that directory, and
      inline + global-sequence clips are still present alongside them.
- [ ] `--textures`/`--skin-dir`/`--anim` all still default to the model's own
      directory when omitted entirely (unchanged from Part 1).
- [ ] `--output`/`--input` positional-fallback and explicit-flag forms both
      work and agree.
- [ ] `README.md`'s `export` subsection matches this checklist's behavior
      1:1 — no stale prose describing the old positional grammar.
- [ ] `DESIGN.md`'s "target — not yet implemented" heading and "Decided"/
      "Open question" framing get updated to plain past-tense fact once this
      lands (current-vs-target discipline, `READABILITY.md` §3.10).
- [ ] `--skel`/`--textures`/`--lod` do **not** grow an `inline` state — their
      absence is correct per the derivation above, not missing coverage.
