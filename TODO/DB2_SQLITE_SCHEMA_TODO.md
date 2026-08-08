# TODO: real relational schema for `husk db2-export` (cross-file join tables)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

`husk db2-export` (`src/cmd_db2.cpp`, `src/dbd.hpp`/`.cpp`) currently
converts exactly one `.db2` file into exactly one flat SQLite table — real
column names/types when `--dbd-dir` resolves a matching WoWDBDefs layout,
generic `field_<N>` otherwise. Verified against real data (`chrmodelmaterial.db2`,
`namesreserved.db2`) and fully tested (`tests/test_dbd.cpp`, `tests/
test_cli_db2.cpp`).

Per Luna's own direct, twice-repeated scope clarification
(`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s own top note): this exporter is
an explicitly separate side project from the real compositing pipeline
(`export` itself reads DB2 bytes directly in-process, no SQLite round-trip)
— but its own stated ambition is bigger than "one flat table per file":
"it's not gonna be just flat tables only, it's gonna have mapping tables
and stuff... I think it will become massively relevant when the world data
implementation starts." Two real purposes: (1) an independent correctness
cross-check for whatever the real in-process DB2 consumer eventually
becomes; (2) a general-purpose local data source for other consumers of
this project's WoW-format work, not just character texture compositing —
explicitly flagged as likely to matter a lot for WMO/ADT world-placement
data too (`../WORLD_COMPLETENESS.md` and its companion `*_TODO.md` files).

That relational ambition isn't built yet. This file tracks it.

## What's already there to build on

WoWDBDefs' own `.dbd` grammar already encodes foreign-key relationships
directly in the `COLUMNS` block — confirmed by reading real files, not
guessed: `ChrModelMaterial.dbd`'s own column list has
`int<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID?`,
i.e. "this column's real values are IDs into `CharComponentTextureLayouts`'
own `ID` column." `src/dbd.cpp`'s `parseColumnType` already sees this token
but currently **throws it away** — it strips everything from `<` onward to
get the base type (`int`/`float`/`string`/`locstring`) and never records
what's inside the angle brackets. This is the concrete, already-identified
next step, not a new investigation.

Separately, real relation columns can be **non-inline** (`$noninline,
relation$` in a `LAYOUT` block, per WoWDBDefs' own grammar spec) — stored
in the section's own `relationshipData` region, not as a normal record
field. `db2::parse` (`src/db2.cpp`) already reads `SectionHeader::
relationshipDataSize` and advances past that region to keep byte-offset
bookkeeping correct, but **never decodes its actual content** — it's
skipped, not parsed. Any real cross-file join involving a non-inline
relation column needs this decoded first; `dbd::resolveFieldNames` today
only handles the inline case (correctly excludes non-inline fields from
the position-matched name list, since they occupy no field-array slot —
but that also means non-inline relation *values* are currently
unreachable through the existing code path at all).

## Concrete plan (staged, each step independently useful)

### Step 1 — capture foreign-key targets in `dbd::Table`

Extend `dbd::parseColumnType` (or add a sibling function) to also return an
optional `(targetTable, targetColumn)` pair when the type token has a
`<Table::Col>` suffix. Add it to `dbd::Column` (a new struct, replacing the
current bare `std::pair<std::string, ColumnType>` in `Table::columns` —
worth doing regardless of this TODO, since a plain pair is already
straining against having more than one piece of per-column metadata).
Low-risk, additive: no existing caller depends on `columns`' exact type
beyond `resolveFieldNames`' internal lookup, which can be updated in the
same change. New unit tests: a synthetic `.dbd` fixture with a `<Table::
Col>` column, asserting the relation target parses correctly; a real-data
test against `ChrModelMaterial.dbd`'s own `CharComponentTextureLayoutsID`
column, skip-gated on `reference/WoWDBDefs` the same way the existing
real-data `dbd` tests already are.

### Step 2 — decode non-inline relationship data

`db2::Section::relationshipDataSize`'s real byte layout needs pulling from
`documentation/wowdev-wiki/md/DB2.md` (not yet read for this specific
region — `db2.hpp`'s module comment doesn't claim this part is verified,
only skipped-safely). Likely a parallel `(recordId, foreignId)` array,
analogous to `idList`/`copyTable`'s existing shape — check the real
`chrmodelmaterial.db2` file's own relationship data (non-zero
`relationshipDataSize`? check first) or find a real file that actually
exercises this path before assuming the wiki's struct is complete, same
"cross-check against real bytes" discipline `db2.hpp`'s existing decode
paths already follow.

### Step 3 — multi-file export mode

New `db2-export` mode (flag TBD, e.g. `--dir <db2-dir>` instead of a single
`<file.db2>`) that exports every `.db2` file in a directory into **one**
SQLite database, each as its own table (current single-file behavior
stays the default/only mode when a single file is given — don't break the
existing, tested, working path). Once Step 1 lands, emit real `FOREIGN
KEY` constraints on inline relation columns pointing at the referenced
table's own table (only when both tables are actually present in the same
export batch — a dangling reference to a table not being exported is a
real, expected case, not an error, and should degrade to a plain column
with no constraint rather than fail the whole export).

### Step 4 — real join verification

Once Step 3 lands, verify a real multi-table join actually works end to
end against real local data — `ChrModelMaterial` -> `CharComponentTextureLayouts`
is the smallest real chain currently fully populated locally (confirmed,
`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Background section). The fuller
`ChrCustomizationOption` -> `_Choice` -> `_Material` chain `CHAR_TEXTURE_
COMPOSITING_TODO.md`'s Stage 3 actually needs is **not** fully verifiable
right now — several of those exact tables are confirmed 0-byte in the
current local `casc-tool` export (`chrcustomization.db2`,
`chrcustomizationcategory.db2`, `chrcustomizationchoice.db2`,
`chrcustomizationoption.db2`, `chrcustomizationreq.db2`) — a real
extraction gap, not something this TODO can work around. Test against
whatever real chain is actually populated; don't assume the 0-byte tables
will re-extract before writing a test that needs them.

## Explicitly not in scope for this file

- The real in-process compositing pipeline itself (`TODO/
  CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 2+) — that's real per-table
  C++ structs feeding `export_materials.cpp` directly, deliberately
  separate from this SQLite side project per Luna's own scope
  clarification. Don't conflate progress on one with progress on the
  other.
- A general nested-array or non-flat-value schema concern
  (`CHAR_TEXTURE_COMPOSITING_TODO.md`'s own "whether any real DB2 row cell
  is itself an array of nested arrays" open question) — no real example
  has ever been found; worth re-checking before assuming the schema needs
  to account for it, not assumed here either.
