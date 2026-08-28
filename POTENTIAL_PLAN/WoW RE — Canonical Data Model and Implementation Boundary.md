# WoW RE — Canonical Data Model and Implementation Boundary

> Follow-up to **Eventual Potential Goals for WoW RE Project** and **Investigation Into Applying No-Graphics-API API to WoW RE Project** (source: [[No Graphics API — Sebastian Aaltonen]]).

## 1. Core Principle

The canonical representation is **not an interchange format, GPU format, renderer format, or Blender format**.

It represents the recovered **meaning of the WoW data**.

> **The canonical format represents the world and its relationships; how that world is stored, represented, or executed does not matter.**

The project therefore separates three fundamentally different concerns:

```text
Definition
    ↓
Character State
    ↓
Resource / Implementation
```

---

# 2. Definition Layer

The definition layer describes:

> **What exists, what is possible, and what is valid?**

For example:

```text
Character
└── Race
    ├── Dwarf
    │   ├── sex
    │   ├── faces[]
    │   ├── hair[]
    │   └── beard[]
    │
    ├── NightElf
    │   ├── sex
    │   ├── faces[]
    │   ├── hair[]
    │   └── ears[]
    │
    └── BloodElf
        ├── sex
        ├── faces[]
        ├── hair[]
        └── ...
```

The definition tree establishes the valid schema.

For example:

```text
NightElf
    has faces
    has hair
    has ears
    does not have beards

Dwarf
    has faces
    has hair
    has beards
    does not have ears
```

The absence of a definition is meaningful.

A canonical character should not need to contain:

```text
beard = null
```

for a Night Elf if `beard` is not a valid concept for Night Elves in the first place.

---

# 3. Character State Layer

Character state answers:

> **What did this particular character choose?**

Conceptually, this can be thought of like a JSON object:

```json
{
    "race": "NightElf",
    "sex": "Female",
    "customization": {
        "face": 12,
        "hair": 7,
        "ears": 3
    },
    "equipment": {
        "head": null,
        "chest": "SomeChestpiece",
        "weapon": "WINDFURY_THE_BLESSED_BLADE_OF_THE_WINDSEEKER"
    }
}
```

This JSON-like structure is only a mental model. The actual canonical representation can be a strongly typed binary structure, structs, tables, or another representation.

The important property is that its **valid structure is determined by the definition layer**.

---

# 4. Definitions Are Not Selections

A crucial distinction:

```text
Definition:
    NightElf.face[12]
```

is not the same thing as:

```text
Character:
    customization.face = 12
```

The first describes **what face option 12 is**.

The second says **this character selected option 12**.

Therefore:

```text
Character A
    race = NightElf
    face = 12

Character B
    race = Dwarf
    face = 12
```

does not imply that both characters reference the same face resource.

Their resolution paths are:

```text
Character A
    ↓
NightElf
    ↓
NightElf customization definitions
    ↓
face[12]


Character B
    ↓
Dwarf
    ↓
Dwarf customization definitions
    ↓
face[12]
```

The number `12` is therefore a **contextual selection/index**, not necessarily a globally unique resource identity.

---

# 5. Contextual Namespaces

A general rule emerges:

> **A definition should live at the narrowest scope that completely determines its identity.**

For example:

```text
Windfury
```

can be globally identified if its meaning does not depend on race.

But:

```text
face #12
```

cannot necessarily be globally identified.

Its meaningful identity may instead be:

```text
(NightElf, Face, 12)
```

or structurally:

```text
NightElf
└── customization
    └── faces
        └── [12]
```

This prevents the canonical model from creating enormous global namespaces for things whose identity is inherently contextual.

---

# 6. Canonical Resources

The canonical layer can refer to **semantic resources**.

For example:

```text
NightElf
└── face[12]
    ├── diffuse_image
    ├── normal_image
    └── corrective_data
```

These names describe meaning.

They do **not** describe implementation.

The canonical layer should not say:

```text
normal_map = UINT64
```

or:

```text
normal_map = VkImage
```

or:

```text
normal_map = BlenderImage
```

Those are implementation decisions.

The canonical layer says:

> This face has a normal image associated with it.

---

# 7. Implementation Layer

The implementation layer answers:

> **How do I physically represent and use this data?**

Different consumers can make completely different decisions.

```text
                 Canonical Face
                      │
              normal_image
                      │
          ┌───────────┼───────────┐
          ▼           ▼           ▼
       Blender       CPU        GFX906
          │           │           │
       Image       pixels      GPU representation
```

For example:

### Blender

```text
normal_image
    ↓
Blender Image
```

### Software renderer

```text
normal_image
    ↓
CPU pixel buffer
```

### Vulkan backend

```text
normal_image
    ↓
Vulkan image / associated resources
```

### GFX906 backend

```text
normal_image
    ↓
GFX906-appropriate physical representation
```

The canonical representation does not need to know which one occurred.

---

# 8. The Three-Layer Mental Model

The entire system can therefore be reduced to:

```text
┌─────────────────────────────────────────────┐
│ DEFINITION                                  │
│                                             │
│ "What exists and what is valid?"            │
│                                             │
│ NightElf → faces, hair, ears                │
│ Dwarf   → faces, hair, beard               │
└──────────────────────┬──────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────┐
│ CHARACTER STATE                             │
│                                             │
│ "What did this character choose?"           │
│                                             │
│ race = NightElf                             │
│ face = 12                                   │
│ hair = 7                                    │
│ ears = 3                                    │
└──────────────────────┬──────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────┐
│ CANONICAL SEMANTIC RESOURCES                │
│                                             │
│ "What things represent those choices?"      │
│                                             │
│ face[12] → diffuse image                    │
│         → normal image                      │
│         → corrective data                   │
└──────────────────────┬──────────────────────┘
                       │
                       │ implementation boundary
                       ▼
┌─────────────────────────────────────────────┐
│ IMPLEMENTATION                              │
│                                             │
│ "How do I actually store/use/render it?"    │
│                                             │
│ Blender / CPU / Vulkan / GFX906 / etc.      │
└─────────────────────────────────────────────┘
```

---

# 9. Getter / Setter Semantics

The canonical API can hide the hierarchy from consumers.

For example:

```cpp
FaceOption* get_face(Character& character)
{
    auto* defs =
        character.race->customization_definitions;

    return &defs->faces[character.customization.face];
}
```

The caller only needs to know:

```cpp
FaceOption* face = get_face(character);
```

The canonical API handles the contextual lookup.

Likewise, setting a selection should validate against the definition tree:

```cpp
bool set_face(Character& character, uint32_t face_id)
{
    auto* defs =
        character.race->customization_definitions;

    if (face_id >= defs->face_count)
        return false;

    character.customization.face = face_id;
    return true;
}
```

The setter therefore enforces the canonical model's validity rules rather than leaving them to the renderer.

---

# 10. The Canonical Layer Must Not Leak Storage

The canonical model should describe:

- identity
- relationships
- meaning
- valid choices
- selections
- semantic resource references
- semantic properties

It should **not** describe:

- memory addresses
- GPU addresses
- allocation strategy
- buffer layout
- texture encoding
- API objects
- descriptors
- command buffers
- CPU/GPU synchronization
- execution strategy

Those belong below the boundary.

A useful architectural test is:

> **Could the same canonical data be consumed by a software renderer, Blender, Vulkan, or a completely different GPU architecture without changing its meaning?**

If yes, the abstraction boundary is probably in the right place.

---

# 11. Physical Layout Is a Separate Compilation Problem

The semantic hierarchy does not dictate physical memory layout.

Conceptually:

```text
Canonical:

NightElf
└── customization
    └── face[12]
```

could become physically:

```text
GPU:

FaceTable
├── NightElf faces
├── Dwarf faces
├── BloodElf faces
└── ...
```

with appropriate offsets or handles.

Or:

```text
GPU:

NightElfFaceTable*
DwarfFaceTable*
BloodElfFaceTable*
```

Or:

```text
GPU:

packed global face array
+ per-race offsets
```

Or something else entirely.

The canonical model does not care.

Therefore:

```text
SEMANTIC HIERARCHY
        ≠
PHYSICAL MEMORY HIERARCHY
```

This distinction is especially important for the eventual GFX906 backend.

---

# 12. Consequence for the WoW RE Project

This changes the purpose of the RE work.

The goal is not merely:

> "Decode M2/PHYS/ADT/etc."

It becomes:

> **Recover the semantic model that those historical formats serialize.**

For example:

```text
WoW M2
   ↓
M2 decoder
   ↓
Canonical Mesh / Skeleton / Animation / Appearance
```

and:

```text
PHYS v1
PHYS v2
PHYS v3
PHYS v4
   ↓
version-specific decoders
   ↓
Canonical Physics
```

Historical format differences remain in the decoder layer.

The canonical model expresses what those versions mean.

---

# 13. Consequence for GLTF

GLTF no longer needs to be part of the canonical architecture.

The preferred path becomes:

```text
WoW
 ↓
Canonical
 ↓
Blender addon
```

rather than:

```text
WoW
 ↓
Canonical
 ↓
GLTF
 ↓
Blender
```

GLTF can remain as an optional exporter:

```text
Canonical
    │
    └──→ GLTF
```

but GLTF is now explicitly a **best-effort projection**.

If GLTF cannot represent something, it can:

- omit it;
- encode it in `extras`;
- use an optional extension;
- or simply refuse to export that feature.

The canonical model does not change to accommodate GLTF.

This eliminates the need for semantic hacks such as representing WoW geosets/vertex groups as fake bones purely to transport them through GLTF.

---

# 14. Consequence for the No-Graphics-API Investigation

> Source: [[No Graphics API — Sebastian Aaltonen]]

The same architectural principle applies at the other end.

The canonical model should not become:

```text
WoW → Vulkan representation
```

Instead:

```text
WoW
 ↓
Canonical semantic model
 ↓
Implementation compiler
 ├── Blender
 ├── CPU
 ├── Vulkan
 └── GFX906
```

Vulkan becomes one implementation substrate rather than the conceptual foundation of the engine.

Likewise, GFX906-specific concepts belong in the GFX906 implementation layer.

The canonical model should never need to know that GFX906 has wave64.

The GFX906 backend gets to exploit wave64 because it understands the physical target.

---

# 15. Architectural Invariant

The project should preserve this boundary:

```text
                 WHAT
                  │
                  ▼
        ┌──────────────────┐
        │ Canonical model  │
        └────────┬─────────┘
                 │
                 │ implementation boundary
                 ▼
                 HOW
```

Or, more explicitly:

> **The canonical layer describes the world and its relationships. Implementation layers decide how that world is stored, materialized, transported, rendered, simulated, or executed.**

That is the core architectural invariant.

Everything below it is replaceable.

Everything above it should remain semantic.