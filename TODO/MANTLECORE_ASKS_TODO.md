# mantlecore asks (open)

Received 2026-11-24 from mantlecore's inbox messages, checked against husk
at ab3f1b5. mantlecore's own detail: `/home/luna/dev/mantlecore/development/
husk_ecosystem_asks.md`, `husk_effects_asks.md`, `husk_effects_handoff.md`,
`husk_wmo_shaders_handoff.md`, `design/bundle-viewer.md`,
`design/gpu-animation-and-cloth.md`. Reply with `inbox_send_feedback` to
`/home/luna/dev/mantlecore` as items close.

## 0.4.0 release (Luna 2026-11-24)

Everything possible goes into 0.4.0, on integration branch `format-0.4`;
anything now understood moves out of `unresolved`. Decided:
- Every branch below merges into `format-0.4`, not master.
- Creatures: display → model by stable id; model refs carry the listfile
  name; optional `classification`.
- Rename `parts[].fog_volumes` (it is not volumes).
- Portal `side` leaves `unresolved`.
- Gravity: an out-of-range packed key is dropped with a warning, so the
  export goes on; gravity rotates with the emitter (Luna's game knowledge).
- Follow-ups 17-21 all go in.

Second round (Luna 2026-11-24), all into 0.4.0:
- Map identity: a new `map` block (Map.db2) on world documents; map
  metadata on tiles. No UiMap / UI region tree yet.
- WMO interior names: an area ref per mesh part (WMOAreaTable).
- Stable ids for every large array, not only `references`.
- Presets become references, inline or external like any other
  reference; external points into a shared appearance-list document,
  inline keeps a bundle self-contained.
- Animation clip names from a community list: allowed, but never
  committed; read from a local checkout under the gitignored `reference/`
  (same pattern as WoWDBDefs and wow.export), with the repo named in docs.
- Every emitted document names its schema (critical).
- Self-documenting schema: `x-source` on every value, lint-enforced, why
  in the description, `husk schema-doc` renders one file.

## Answer only (already in husk)

1. Per-chunk areas: terrain `areas[]` + per-chunk u32 index exist
   (`schema/model.schema.json` "areas", `src/writers/terrain_bundle_writer.cpp`).
2. External `.anim` sequences are exported (`--anim auto`,
   `buildExternalAnimBlobs` in `src/cmd_export_canon.cpp`). Check that
   export-world's model pass doesn't pass `--anim inline` before answering.

## Bugs

3. **Packed particle gravity**: M2Particle flag 0x800000 stores a compressed
   vector in the gravity float; husk reads it as a float, NaN → `null`
   (1,278 emitters azeroth, 801 map 2912). `src/m2_scene.cpp` gravity track,
   `src/writers/placement_set_writer.cpp`. Decode per wowdev, verify on
   corpus emitters. Luna 2026-11-24: write the decoded vector, schema 0.4.0,
   held back to ship with today's other breaking changes in one release.
   Done on `format04-gravity` (6e128b3, 1020/1020 tests): `particle_system.
   gravity` is an `animated_vec3` in the model frame. 80% of sampled
   emitters are packed, and every one was wrong before (NaN or garbage
   floats). At integration: a MANTLECORE_MIGRATION.md line, and
   `dump-chunks`' `gravity_track` still prints packed keys as floats.
   Unchecked: whether the client rotates gravity with the emitter.
4. **Zone fog `density` labelled `1/yard`**: done on
   `format04-fog-density`. Fog keys gain `extinction`/`height_extinction`
   (1/yard, `fog_density_to_extinction`, `0.0005 · density`, grade ref:
   WebWowViewerCpp, not client code); raw values under `fog.source`.
   Open: verify the constant in the client executable (needs a
   disassembler, not in the flake) or by screenshot.
5. **MFOG medium placed in its sphere** (`src/wmo_effects_input.cpp`
   `linear_fog_to_extinction`): the sphere is where the fog applies to a
   viewer inside, not a volume. Luna 2026-11-24: drop the derived medium;
   describe MFOG as the fog seen from inside the listed parts.
   Done on `mantlecore-fog` (tests pass): 69243a0 relabels density as the
   client's parameter (unit `1`; the CPU-side transform to `k` in
   `exp(−e·k)` is unknown), not breaking. 7a0d47a removes MFOG's medium
   content (readers lose optional data), a 0.4.0 candidate; renaming
   `parts[].fog_volumes` is another.

## Investigations

6. Shader 23 `cb2[5]`: unresolved without a client capture (branch
   `mantlecore-wmo-shaders`, 0be89b7). In 10,361 sampled materials
   sidnColor is always 0xff000000 and diffColor takes 88 values; the tint
   weight is nonzero on 51% of vertices, so the choice is visible. Evidence:
   WIKI_FINDINGS/WORLD.md "Shader 23's tint".
7. WMO portal `side`: resolved on branch `mantlecore-portal-side`
   (f7b5b16): +1 = the owning part lies where `dot(normal, p) + distance > 0`;
   the centroid misses were exterior groups. Open: Luna's call on moving
   `side` out of `unresolved` (breaking, 0.4.0 candidate).
8. Cloth: does the client mark cloth (`.phys`, M2 flags, geosets)? Answer
   yes with data, or no.
9. Suramar ley lines: lead is the `procleyline*` liquid shader family
   (`WIKI_FINDINGS/RENDERING.md`) → LiquidType rows + MH2O on map 1220.
10. Wind response time per effect class: client data (M2Particle wind
    fields?) or mantlecore's default.

Items 8, 9, 10, 15 answered on branch `mantlecore-findings` (34ef52b,
fcd3763, 0b23f79; WIKI_FINDINGS/M2.md, PHYS.md, WORLD.md,
`REFACTOR/ANIMATION.md`):
- Cloth: no cloth flag in the client. 39/263 capes carry rigid-body chains
  (`PFDC`/`PFID` + bone flag 0x400); 148 have cape key bones 36-40; ships,
  sails and tabards have no physics.
- Ley lines: mostly models (`7fx_leyline_doodad_*`, `7ne_ancient_leyline*`);
  liquid type 868 on 3 tiles of map 1220, already exported.
- Wind: no response time in the client; DynamicWind flag (bit 31) on ~1.3%
  of emitters; mantlecore keeps its default.
- Walk/run/death are mostly inline; external `.anim` is emotes and
  sit/sleep. 12.x AnimationData has no Name column, so clips are unnamed
  everywhere.

Follow-ups from those findings (not started):
17. Physics from `PFDC`/`PFID` into `resources.physics` (today only
    `--phys` or a sibling `.phys`).
18. Global-sequence loop length (`model.globalLoops` parsed, never
    written); additive schema field.
19. AnimationData `Name` warning on layouts without that column; optionally
    export `AnimationData.Fallback`.
20. Bone flags on skeleton joints (at least 0x400).
21. Check: `db2-export` LiquidType `Float_*` columns look like raw int bits.

## New exports

11. AreaTable flags on world `zones`: done on `mantlecore-world-db2`
    (65a2014, tests pass), additive. `source.flags`, `mount_flags`,
    `flag_names` (bit names from wowdev.wiki's 10.2 tables).
12. Graveyards: not in the client. `WorldSafeLocs.db2` ships in no local
    build (none after 8.x); `WaypointSafeLocs` holds portal/boat/tram
    nodes, not graveyards. Positions and area served are server data.
    Documented in REFACTOR/BUNDLE_FORMAT.md; nothing exported.
13. Technique ops `min`, `dot`, `tangent_frame`: done on
    `mantlecore-wmo-shaders` (0be89b7, tests pass). Parallax's
    `tangent_view` renderer value is replaced by `view_position` +
    `view_normal`; a consumer reading `tangent_view` breaks, so the commit
    ships with 0.4.0 unless Luna says otherwise.
14. Creature catalog: Creature.db2 type/family, scale, display ids → model
    bundle + preset. Luna 2026-11-24: a new document kind, one `creatures`
    document per corpus (stable ids per entry, display refs to model
    bundles in the store), schema-validated, in the 0.4.0 release. Not per
    zone: which creatures live where is server data, not in the client.
    WIP e0df25b on `format04-creatures`, successor agent building/testing.
    Open for Luna: display→model by `references` index vs stable id;
    listfile name on model refs; `classification` (elite/rare/boss).
15. Animation guide: short doc on clip names, which live in `.anim`.

## Waiting on mantlecore

16. Skin composite blend mapping (mode 15, layer order): their blend lab
    record (S4) arrives later; `REFACTOR/MATERIALS.md` keeps
    `blend_mapping: unverified` until then.
