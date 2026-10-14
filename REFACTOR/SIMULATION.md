# SIMULATION.md — living creatures and NPCs in the canonical format

**Status: proposal (2026-10-15), nothing built.** Decisions marked
**(Luna)** are settled; the rest is proposal. Reference (Luna,
2026-10-15): "How I Simulate Over a Million Persistent NPCs For My
Procedural City" (YouTube `nBV0yAAJUf0`; video and transcript in the
gitignored `reference/npc_simulation/`). Its ideas are in §7, marked
"(reference)".

## Goal (Luna)

Creatures and NPCs with lives, driven by behaviour rather than
pre-planned paths. This is our own system, not an emulator: server-side
game data (AI scripts, waypoints, spawns) is neither needed nor wanted.
The format describes:
- what the world offers (where one can walk, sit, drink, keep warm);
- what each body can do (how it moves, what it fits through, what it
  needs);
- who each individual is (home, work, purpose, hobbies, relationships).

The runtime decides, each step, what everyone does next.

- **Rules are about environments, not routes**: "this creature lives near
  water in forests", never "this creature walks this path". Resources
  such as water are sought out by need.
- **Speed is a slider**: walk and run blend continuously by speed, with
  no switch between them.
- **Every creature has IK**: feet meet the ground and ground clutter.
- **The world gets navmeshes**, one per kind of body: a quadruped climbs
  slopes a biped can't. Portals are short edges for those allowed to use
  them.
- **Size is approximate**: a creature's clearance comes from its scale and
  how small a gap it squeezes through. A cat fits where a dog of the same
  size doesn't, within "close enough".
- **NPCs perceive and choose**: sittable objects, interactable agents, a
  near-future goal set by a light planner (a behaviour system or a small
  LLM), carried out by a behaviour tree or a second model.
- **Temperature matters**: cold makes creatures shiver and seek shelter,
  fires, interiors; openings like windows and doors leak heat.

## 1. Invariants

1. **No pre-planned behaviour in data.** Documents describe places, bodies,
   needs and identities. Paths, schedules-in-progress and choices are
   computed at runtime.
2. **Every value says where it came from**, as one of four layers (§2), in
   the document's `provenance` and per field where a document mixes them.
   A consumer can always tell "the game says" from "we inferred" from
   "we decided".
3. **Bundles hold no runtime state.** Hunger, memory, the current goal and
   path live in the simulation only.
4. **Derived documents are deterministic** from their source build and
   their parameters, so they are stored and versioned like any export
   (`CORPUS_EXPORT.md`): a patch rebuilds only what changed.
5. **Agents act through a fixed action vocabulary** (§7.3) and animate
   through animation *states* (§5), never clip indices. A species
   without a clip for a state falls back the way the source says.
6. **Static costs are geometric.** Anything that varies with weather, time,
   or the agent (temperature, danger, preference) is applied when a path
   is queried, never baked into the navmesh.

## 2. Layers

| Layer | Made by | Examples |
|---|---|---|
| Source | husk, from game files | collision, liquids, surface types, WMO groups and portals, clips, `AnimationData` |
| Derived | an offline build over source | navmeshes, clearance, affordances, habitat descriptors, thermal shape |
| Authored | Luna, tools, or an LLM pass | species needs, climate, individuals, places, behaviour library |
| Runtime | the simulation; never stored | need values, memory, current goal and path, lit/unlit fires |

An authored value overrides a derived one of the same thing; both stay
visible (the override names what it replaces).

## 3. Source: what husk carries or should carry

Already in bundles: terrain heights and holes, WMO collision faces (face
channel `wmo.poly_flags`), M2 collision, liquids with their LiquidType,
surface types (`resources.surface_types`), WMO portals, WMO group flags,
placed lights, particle emitters, ground-cover rules, doodad placements
with names.

To add (all present in the 12.1.5 client: `dbfilesclient/` and the M2s):
- **Clip metadata**: each animation clip's `animation_id` and sub-id,
  frequency, variation and alias chain, replay range, blend time, loop
  flags and `movespeed` (the M2 sequence record; husk parses the chain in
  `m2_animation.cpp` but the bundle doesn't carry it).
- **The animation vocabulary**: `AnimationData` (state names, fallback
  ids, flags) as one shared document, referenced like techniques.
- **Per-display animation overrides**: `AnimReplacementSet`,
  `AnimReplacement`; scripted and partial-body animation: `AnimKit*`.
- **Emotes**: `Emotes`, `EmotesText*` (state, sound).
- **Sounds per creature**: `CreatureSoundData`, `NPCSounds`, `SoundKit`
  (animation events are already in `scene.events`).

## 4. Derived: what the world offers

### 4.1 Navigation

- **Locomotion profiles** are records of what a kind of body can do:
  maximum slope, step height, maximum drop, whether it swims, climbs or
  flies, and a minimum clearance. These are our parameters, not game
  data. A starting set: `biped`, `quadruped`, `small` (critters),
  `swimmer`, `flyer`. A species names one.
- **One navmesh per profile, tiled on the ADT grid**, so a tile rebuilds on
  its own. WMOs contribute their collision faces; indoor and outdoor
  meshes join at WMO portals and entrances.
- **Clearance on polygons, not radius per mesh.** Each polygon (or edge)
  stores the gap it offers: width and height. A creature fits if its
  body (§5.1) does. Rejected: one navmesh per creature size (too many
  meshes, and sizes are continuous).
- **Off-mesh links**: jumps, drops, ladders, doors, and portals and
  transports. Each has a travel time and an optional **access
  condition**, so pathing treats a portal as a short edge only for an
  agent with access (§6.2 `access`).
- **Area costs** from geometry and surface: surface type, liquid depth,
  road (from ground textures/effects, inferred). Per-agent preferences
  multiply these at query time (invariant 6).
- **A coarse travel graph** per profile over the navmesh tiles (regions as
  nodes, crossings and links as edges, with travel times), for routes
  across a map and for the coarse tier (§7.1). The reference uses a street
  graph for far goals and the navmesh only for the last stretch; this
  graph is that street graph, derived instead of authored.

### 4.2 Affordances

Typed points on placed objects and places: where something can be done.
Each has a position, a facing where it matters, a capacity, and the
object or place it belongs to.

| Type | Means | Likely source |
|---|---|---|
| `seat` | sit, with facing and seat height | model names (`chair`, `bench`, `stool`), inferred |
| `bed` | sleep | model names, inferred |
| `workstation:<kind>` | forge, anvil, counter, loom, ... | model names, inferred |
| `water_source` | drink; `drinkable` from LiquidType (water yes, magma and slime no) | liquids, source |
| `food_source` | eat, by diet (grazing, fruit, prey, kitchen) | ground cover and doodads, inferred |
| `heat` | warmth, with strength and falloff; `switchable` | fire emitters, warm placed lights, hearth/brazier/forge names, inferred |
| `shelter` | cover from weather | WMO interiors, overhangs, inferred |
| `door` | passage with state | WMO portals and models, inferred |
| `lookout`, `gathering_spot` | social or watch spots | authored |

Inferred affordances say so (`provenance`), and an authored list can add,
move or remove them.

### 4.3 Habitat descriptors

Per navmesh region: zone and area, surface mix, liquid nearby and its
type, indoor or outdoor, elevation, vegetation density (ground cover),
exposure to sky and wind. A species' preferences (§5.4) score against
these, so "where would this creature live" is a query.

### 4.4 Temperature

Temperature is a field the runtime evaluates; the bundle stores what
shapes it:
- **Climate baseline** per zone: base temperature and its swing by time of
  day and season (authored; no client source is known). The weather
  system adds its offset at runtime.
- **Heat sources**: `heat` affordances (§4.2), lit or not at runtime.
- **Insulation** per region: outdoors 0, WMO interiors high (group
  indoor/outdoor flags), caves, tents and covered areas between.
- **Openings that leak heat**: WMO portals (doors, windows) link an
  interior to outside, so a room by an open door is colder than an
  inner one.
- **Exposure modifiers**: being in water (chills fast), elevation, wind
  exposure (from how open the terrain is; a heuristic, labelled so).

Paths consider temperature at query time, against each agent's comfort
(§5.4): a cold-adapted creature isn't steered off snow because a human
would be.

## 5. Species: what a kind of body is and needs

A `species` document (new kind) per kind of creature; many displays and
individuals share one.

### 5.1 Body

- Skeleton and appearance refs (the model documents and presets that
  exist today).
- **Capsule** from the model's bounds times scale.
- **Squeeze factor** (0–1): how much smaller than its capsule a gap may
  be, e.g. cat 0.5, dog 0.8. Clearance check: `gap ≥ capsule × squeeze`.
  Approximate by design.
- Locomotion profile ref (§4.1), mass (for clutter interaction).

### 5.2 Locomotion

- **Gait blend space**: per gait set (ground, swim, fly), a 1D space over
  speed. Each entry is a clip state with its native speed (the clip's
  `movespeed`); the runtime blends neighbouring entries by the current
  speed and scales playback, so speed is continuous.
- Turn rate, acceleration, preferred cruising speed, sprint limit.

### 5.3 IK rig

- Leg chains (hip to foot), foot bones, ground probe points, and for
  quadrupeds the body pitch and roll response to terrain.
- Bone roles are inferred from hierarchy and names; an authored override
  wins.
- Ground clutter reacts on the consumer side: the species says where its
  feet are and how heavy it is.

### 5.4 Needs, senses and preferences

- **Needs**: thirst, hunger, rest, warmth, safety, social; each a decay rate
  and what satisfies it (affordance types; diet for food).
- **Thermal comfort range** and **insulation** (fur, feathers; clothing for
  NPCs): outside the range the warmth need drops. Mild: prefer warmer
  areas. Moderate: the `shiver` (or `pant`) state layers over the
  current action. Severe: shelter or heat becomes the goal.
- **Senses**: vision range and field of view, hearing, smell.
- **Habitat preferences**: scores over habitat descriptors (§4.3).
- **Activity cycle** (diurnal, nocturnal, crepuscular), **sociality**
  (solitary, pair, herd, pack), **temperament** (skittish, territorial,
  curious).
- **Population density** per habitat, for spawning creatures that have no
  individual record.

### 5.5 Animation

The species maps the action vocabulary's states (§7.3) to animation states
from `AnimationData`, with the display's replacements applied. Idle
variety uses the clips' frequency, variation chain and replay range.
States the source lacks (`shiver`, `pant`) are added to the vocabulary
with a fallback, marked authored.

## 6. Individuals and places

### 6.1 Places

Named regions over the world: a type (home, inn, smithy, market, field,
den, nest), a boundary (WMO groups, a volume, or a navmesh region), its
affordances, capacity, and owner or occupants. Seeds come from source
(WMO group names, AreaTable, interiors); the rest is authored or
generated.

A place lists its **stationary spots**: where an occupant stands, sits
or sleeps while there (the reference's "building map" that anchors
project onto). Each place also gives its population capacity, which
feeds the density map (§7.2).

### 6.2 Individuals

Most individuals are **generated from a seed**, not stored: the world's
population seed plus the individual's index gives its species,
appearance, traits, home, work and relationships, the same every run
(reference: each NPC's seed is its pixel coordinate plus the city seed).
An `individual` document exists only for an authored one, or to pin
values over a generated one. Persistent changes made at runtime (a new
friend, a moved home) are the simulation's save data, not bundle data
(invariant 3). Creatures without a life worth tracking are species
instances spawned by density.
- Species and appearance (a preset).
- Name, personality traits.
- **Home** and **work** (place refs), role at work, a schedule *template*
  (tendencies by time of day, not appointments).
- **Purpose and hobbies**: places and affordance types they gravitate to.
- **Relationships**: typed edges to other individuals.
- **Access**: faction and keys, which open doors and portal links (§4.1).

## 7. Decision layer

### 7.1 Two tiers (reference)

Everyone is simulated all the time, but not at the same detail:
- **Coarse tier, for every individual**: an approximate position, a
  current activity phase (home, travelling, at work, at a hobby place,
  satisfying a need) and a destination, advanced cheaply. The reference
  keeps a million of these on the GPU, one pixel each (position in R/G,
  phase in B, normalized). Routes follow the coarse travel graph
  (§4.1); needs advance as rates, without perception.
- **Detailed tier, near the observer**: navmesh pathing, avoidance, IK,
  perception, the planner and executor below. Only a few hundred at once
  (the reference: 200–300).
- **Promotion**: an individual entering range spawns at its coarse
  position, projected onto something valid (on a walkable surface if
  travelling, onto a stationary spot of its place if stationary).
- **Anchors**: while detailed, the coarse tier keeps giving it an anchor
  (where the coarse simulation says it should be), so the two don't
  drift apart. The reference re-reads every few seconds, off the main
  thread.
- **Retention**: a detailed individual still near the observer is kept
  even when its coarse position leaves range, so nobody vanishes in
  view; it rejoins its anchor when it can.
- **Interaction** (talking, following) detaches an individual from its
  anchor; it returns to it afterwards.
- **Demotion, beyond the reference**: when a detailed individual leaves
  range, its state is written back to the coarse tier (need levels,
  current goal as a phase and destination). The reference throws the
  detailed state away, which is fine for fixed schedules but would lose
  goals a planner chose.

The planner and executor (§7.3) run only in the detailed tier; the
coarse tier follows schedule templates and need rates, so an LLM never
runs for a million agents.

### 7.2 Population density (reference)

Homes, workplaces and creature populations are sampled from a density
map, so nobody lives in a river. The reference builds a coarse density
grid once (from building height and roads; rivers and roads are 0),
computes its cumulative distribution once, and draws locations by
inverse transform sampling. Here the density comes from place
capacities (§6.1) and, for creatures, habitat scores times species
density (§5.4): one grid per map and kind of population, derived and
stored with the population.

### 7.3 Planner and executor

Format-agnostic.
- **Perception** queries return affordances, agents and places within the
  agent's senses, with what each offers.
- **Planner**: a utility system, a behaviour system or a small LLM sets a
  *near-future goal* from needs, schedule template, purpose and what it
  perceives ("get warm", "eat at the inn", "talk to the smith").
- **Executor**: a behaviour tree (shared as data, like techniques) or a
  second model turns the goal into actions.
- **Actions**: `go_to(place | point)`, `use(affordance)`,
  `interact(agent, kind)`, `emote(state)`, `wait`, `flee(from)`,
  `follow(agent)`. Each action names the animation states it plays.

## 8. Documents

| Kind | Layer | Holds |
|---|---|---|
| `model`, presets (exist) | source | appearance; clips gain metadata (§3) |
| `animation_vocabulary` | source | `AnimationData` states and fallbacks, plus authored states |
| `navmesh` (per tile, per profile) | derived | polygons with clearance and area type; off-mesh links; binary payload beside a JSON header |
| `affordances`, habitat, thermal shape | derived | per tile, as placement sets of typed points and regions |
| `locomotion_profile` | authored | the profile records (§4.1) |
| `species` | authored + source refs | §5 |
| `place` sets, `individual` | authored | §6 (individuals only where authored or pinned) |
| `population` | authored + derived | per world: population seed, generator parameters, density grids and their distributions (§7.2) |
| coarse travel graph | derived | per tile and profile, beside the navmesh (§4.1) |
| `behaviour` | authored | behaviour trees as data |
| `climate` | authored | per-zone baselines (§4.4) |

## 9. Open decisions

1. **Who builds navmeshes (Luna)**: husk, offline into the store
   (deterministic, versioned per build, changed tiles only), or MantleCore
   at load. Proposal: husk. It needs a navmesh library (e.g. Recast),
   added through the flake with permission.
2. **Profiles and their numbers**: the starting set and each one's limits.
3. **Where individuals live**: in world bundles, or a separate population
   bundle per world (proposal: separate; it changes on a different
   schedule from the game build).
4. **The planner and executor (§7.3)**: utility system, behaviour trees,
   LLM, or a mix. The reference covers tiers, spawning and density but not
   decision-making (its NPCs follow fixed home/work schedules).
5. **Climate source**: authored per zone, or derived from zone names,
   skyboxes and weather data where some exists (unchecked).
6. **Coarse tier placement**: on the GPU as in the reference, or on the CPU
   (a WoW world's population is likely far below a million; unchecked).

## 10. First steps

Each stands on source data and doesn't wait on §9:
1. Clip metadata and the `AnimationData` vocabulary in bundles (§3).
2. Check the navmesh inputs are complete for a builder: collision,
   portals, liquids, holes, surface types (§4.1).
3. The affordance inference pass, labelled inferred (§4.2).
