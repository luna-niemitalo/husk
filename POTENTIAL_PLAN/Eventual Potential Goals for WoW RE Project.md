# Eventual Potential Goals for WoW RE Project

> Long-term exploration goals. These are deliberately broader than the current implementation scope and should not be interpreted as immediate implementation requirements.

## 1. Establish a Complete Semantic Model of WoW Content

The ultimate goal of the RE project is not merely to decode file formats, but to reconstruct the **semantic content model** that those formats represent.

The project should progressively move from:

```text
bytes → structs → fields
```

toward:

```text
bytes
  ↓
format/version decoder
  ↓
semantic interpretation
  ↓
canonical WoW content model
```

Examples:

- `BODY`, `BDY3`, `BDY4` → canonical physics bodies
- `SHAP`, `SHP2` → canonical collision shapes
- `WELJ` / later joint representations → canonical joints
- `.bone` correction matrices → canonical facial/customization corrections
- M2 animation tracks → canonical animation data
- SKIN data → canonical mesh/material/texture-layer information
- ADT/WMO data → canonical world/terrain/scene representation

The canonical model should not inherit historical format distinctions unless those distinctions have actual semantic significance.

---

## 2. Treat WoW File Formats as Serialization Formats

The original WoW file formats should be regarded as **input serialization formats**, not necessarily as appropriate runtime layouts.

For example:

```text
PHYS v1 ─┐
PHYS v2 ─┤
PHYS v3 ─┤
PHYS v4 ─┤
PHYS v5 ─┘
          ↓
   Canonical Physics
```

The same principle should apply to all versioned or historically evolved formats.

The RE project should therefore distinguish between:

- **on-disk representation**
- **canonical semantic representation**
- **runtime representation**
- **GPU representation**
- **debug/export representation**

This prevents historical implementation decisions from becoming permanent architecture.

---

## 3. Maintain GLTF as an Interchange and Visualization Target

GLTF should remain valuable, but should not necessarily become the canonical internal representation.

GLTF is useful for:

- Blender inspection
- geometry visualization
- skeleton inspection
- material inspection
- debugging
- regression testing
- sharing extracted assets
- comparing RE results with other tools

The architecture should instead look like:

```text
Canonical representation
        │
        ├──→ GLTF exporter
        ├──→ debug/dump tools
        ├──→ Blender-oriented tools
        └──→ runtime compiler
```

Creative extensions or `extras` can be used where GLTF cannot naturally express WoW-specific semantics.

For example, the existing `.bone` work already demonstrates this approach: corrective matrices can be represented as inert GLTF metadata without pretending that GLTF itself is the runtime model.

---

## 4. Build Version-Agnostic Semantic Compilers

Each WoW format version should have a decoder responsible for understanding its historical representation.

The decoder should produce the same canonical structure whenever the semantic meaning is equivalent.

For example:

```text
decode_PHYS_v1()
decode_PHYS_v3()
decode_PHYS_v4()
        │
        ▼
CanonicalPhysics
```

This provides a clean separation between:

- reverse engineering
- format compatibility
- semantic interpretation
- engine implementation

It also makes future discoveries much easier to incorporate.

---

## 5. Reconstruct the Engine's Actual Data Relationships

The project should increasingly focus on **relationships between assets**, not merely individual file formats.

Examples include:

```text
Model
 ├── Skeleton
 │    ├── Bones
 │    ├── Animation
 │    └── Corrections
 │
 ├── Meshes
 │
 ├── Materials
 │
 └── Appearance
```

and:

```text
Physics
 ├── Bodies
 ├── Shapes
 ├── Shape types
 └── Joints
```

The existing PHYS investigation already demonstrates that cross-chunk references can be validated across a substantial corpus rather than inferred from isolated files.

The long-term objective is to reconstruct these relationships sufficiently well that the engine can consume the content without reproducing Blizzard's original internal implementation.

---

# 6. Eventually Reconstruct a Modern WoW-Compatible Engine

The ambitious endpoint is:

> Load original WoW content and produce substantially the same visual/behavioral result through a newly designed engine.

This does **not** mean reproducing Blizzard's source architecture.

Instead:

```text
Original content
      ↓
RE/import pipeline
      ↓
Modern canonical representation
      ↓
Modern engine
```

The engine should be free to use completely different:

- memory layouts
- scheduling
- rendering architecture
- animation architecture
- caching
- streaming
- physics implementation
- GPU execution model

while retaining compatibility with the original content.

---

# 7. Investigate Hardware-Specialized Runtime Compilation

Once the semantic model is sufficiently complete, investigate compiling it into hardware-specific runtime representations.

For the Radeon VII / GFX906 target:

```text
Canonical data
      ↓
GFX906 runtime compiler
      ↓
GPU-addressable structures
      ↓
GFX906 execution
```

Potential areas include:

- wave64-oriented data layouts
- FP16 intermediate representations
- SoA/AoS transformations
- GPU pointer graphs
- persistent GPU-resident data
- batched compute workloads
- explicit LDS usage
- GFX906-specific kernel specialization

The semantic representation should remain independent of these decisions.

---

# No Graphics API API

> Source: [[No Graphics API — Sebastian Aaltonen]]

## 1. Concept

Investigate an engine architecture where **graphics APIs are implementation substrates rather than the engine's primary conceptual model**.

The engine should think in terms of:

```text
memory
data
execution
synchronization
queues
images
addresses
kernels
```

rather than making concepts such as:

```text
VkBuffer
VkDescriptorSet
VkPipeline
VkRenderPass
```

the fundamental architecture of the engine.

This does not necessarily mean eliminating Vulkan.

It means avoiding making Vulkan's worldview the engine's worldview.

---

## 2. Proposed Conceptual Model

Instead of:

```text
Asset
 ↓
Graphics resource
 ↓
Descriptor
 ↓
Pipeline
 ↓
Command buffer
 ↓
Driver
 ↓
GPU
```

investigate:

```text
Asset
 ↓
Canonical data
 ↓
Runtime data graph
 ↓
GPU-visible memory
 ↓
Execution
```

Vulkan can then implement portions of this model where appropriate.

ROCr/HSA may implement compute-oriented portions.

The underlying AMDGPU interface ultimately provides the bridge to the hardware.

---

## 3. Pointer-Native GPU Data

Investigate representing relationships between runtime objects using GPU-visible pointers.

Conceptually:

```cpp
struct Character {
    Skeleton* skeleton;
    Mesh* mesh;
    Appearance* appearance;
};

struct Skeleton {
    Bone* bones;
    AnimationSet* animations;
    CorrectionSet* corrections;
};
```

The exact representation would need to account for GPU virtual addressing, synchronization, relocation, residency, and lifetime.

The important research question is whether these structures can form useful GPU-resident data graphs without requiring graphics-resource abstractions for every relationship.

---

## 4. Data Transformation Instead of Graphics Operations

Investigate whether operations currently represented as graphics passes can instead be represented as bulk data transformations.

For example:

```text
texture layer composition
        ↓
bulk texel transformation
        ↓
GPU memory
```

rather than:

```text
texture
 ↓
render target
 ↓
pipeline
 ↓
draw
 ↓
blend
 ↓
another render target
```

Similarly:

```text
animation tracks
        ↓
bone transforms
        ↓
skinned vertices
```

could be treated as a chain of compute transformations.

---

## 5. Hardware-Honest Execution

The engine should not assume that every workload belongs on the GPU.

Instead, investigate execution based on workload structure:

| Workload | Potential execution model |
|---|---|
| Animation | Wide GPU compute |
| Skinning | Wide GPU compute |
| Particle evaluation | Wide GPU compute |
| Texture composition | Batched GPU compute |
| Terrain generation | GPU compute |
| BSP traversal | CPU / scalar / selectively GPU |
| Small physics graphs | CPU |
| Irregular graph traversal | CPU or specialized GPU |
| Streaming | CPU / asynchronous I/O |
| Asset decoding | CPU/GPU depending on workload |

The objective is not "GPU everything."

The objective is:

> **Do not force a workload through an abstraction merely because the original engine did.**

---

## 6. Investigate GFX906 as a First-Class Target

The Radeon VII provides an unusually interesting target because GFX906 exposes:

- wave64 execution
- scalar/vector execution
- LDS
- GPU virtual addressing
- explicit memory operations
- strong FP16 capabilities
- mature LLVM AMDGPU support
- ROCr/HSA
- HIP
- Vulkan/RADV

The investigation should determine how much of the proposed model can be implemented using:

```text
Canonical data
      ↓
ROCr/HSA
      ↓
AMDGPU
      ↓
GFX906
```

without requiring a traditional graphics abstraction.

---

# Investigation Into Applying No-Graphics-API API to WoW RE Project

> Source: [[No Graphics API — Sebastian Aaltonen]]

## 1. Research Question

The central question is:

> **Can WoW's existing content model be transformed into a GPU-addressable, data-oriented runtime model without making a graphics API the architectural center of the engine?**

Secondary questions:

- Which WoW systems naturally become GPU data graphs?
- Which systems require descriptors or specialized hardware resources?
- Where does Vulkan provide genuinely necessary functionality?
- Where is Vulkan merely providing historical/API-level organization?
- Where does RADV add necessary translation?
- Where can ROCr/HSA provide a more direct execution path?
- Which workloads benefit from GFX906 wave64?
- Which workloads benefit from FP16?
- Which workloads should remain CPU-side?

---

## 2. Build a Canonical Representation First

Before attempting hardware-specific optimization, define semantic structures.

For example:

```text
CanonicalPhysics
CanonicalSkeleton
CanonicalAnimation
CanonicalMesh
CanonicalMaterial
CanonicalAppearance
CanonicalTerrain
CanonicalWorld
```

Version-specific decoders then populate these structures.

```text
M2 vX
SKIN vX
PHYS v1
PHYS v3
PHYS v4
BONE v1
...
        ↓
Canonical model
```

This creates a stable research target independent of the original file formats.

---

## 3. Add Multiple Runtime Backends

The canonical representation should eventually be capable of producing different layouts.

```text
CanonicalAnimation
       │
       ├── CPU layout
       ├── debug layout
       └── GFX906 layout
```

For GFX906, investigate:

- SoA layouts
- packed FP16 fields
- wave64-friendly batches
- GPU pointers
- contiguous allocation
- LDS staging
- persistent GPU residency

This allows experiments without contaminating the semantic model with hardware-specific assumptions.

---

## 4. Compare Vulkan and ROCr/HSA

Implement deliberately tiny versions of the same operation through different layers.

For example:

```text
Operation:
    transform N vertices
```

Implement:

```text
Vulkan compute
ROCr/HSA dispatch
HIP
```

and inspect:

- setup cost
- memory allocation
- dispatch cost
- synchronization
- shader/kernel representation
- generated ISA
- amount of API state required
- amount of driver work
- actual GPU execution

The purpose is not merely performance benchmarking.

The purpose is to identify:

> **Which pieces are hardware requirements, and which pieces are abstraction requirements?**

---

## 5. Follow the Stack Downward

For representative workloads, trace:

```text
Canonical engine operation
        ↓
Vulkan / HIP
        ↓
RADV / ROCr
        ↓
AMDGPU
        ↓
LLVM AMDGPU
        ↓
GFX906 ISA
```

For each layer, record:

```text
What information is introduced?
What information is transformed?
What information is discarded?
What state exists only because of the API?
What state is actually required by the hardware?
```

This should eventually produce a map of the **real abstraction boundaries**.

---

## 6. Candidate First Experiments

Start with workloads where the distinction should be obvious.

### Experiment A — GPU pointer graph

Construct:

```text
Character
 ├── Skeleton*
 ├── Mesh*
 └── Appearance*
```

in GPU-visible memory.

Have a kernel follow those pointers and perform a trivial operation.

Inspect the generated GFX906 ISA.

---

### Experiment B — Animation

Take a small real WoW animation.

Transform:

```text
animation tracks
        ↓
bone matrices
```

Compare:

- CPU implementation
- HIP
- direct ROCr/HSA
- wave32
- wave64
- FP32
- FP16 intermediates

---

### Experiment C — Skinning

Transform:

```text
bone matrices
        ↓
vertices
```

using GPU-resident pointers.

Investigate whether the optimal representation differs substantially from the GLTF representation.

---

### Experiment D — Appearance composition

Take a real appearance stack and represent:

```text
AppearanceJob[]
```

as a single GPU-resident batch.

Investigate:

- one dispatch vs many
- FP16 blending
- texture-array organization
- cache persistence
- asynchronous cache-miss processing
- disk-backed composite caching

The cache should remain event-driven: an appearance should only be recomputed when its cached result does not exist.

---

### Experiment E — Physics

Take a real `.phys` file and deliberately **do not GPU-accelerate it initially**.

Use it as a control case.

The objective is to demonstrate that a hardware-honest architecture can decide:

```text
"this is better on the CPU"
```

without needing to pretend every subsystem belongs in the GPU pipeline.

---

## 7. GLTF's Role in the Investigation

Continue exporting to GLTF.

Use it as:

```text
                 Canonical representation
                    /          \
                   /            \
                  ▼              ▼
              GLTF export    GPU compiler
                  │              │
                  ▼              ▼
             visualization    GFX906
```

This gives the RE project a stable human-observable output while the runtime architecture evolves independently.

GLTF should therefore be treated as a **projection of the recovered semantics**, not necessarily the recovered semantics themselves.

---

## 8. Eventual Architecture

The long-term architecture being investigated is roughly:

```text
                         WoW CONTENT
                             │
                             ▼
                    ┌──────────────────┐
                    │ Format decoders  │
                    │ v1/v2/v3/...     │
                    └────────┬─────────┘
                             │
                             ▼
                 ┌────────────────────────┐
                 │ Canonical WoW Model    │
                 │                        │
                 │ Mesh                   │
                 │ Skeleton               │
                 │ Animation              │
                 │ Appearance             │
                 │ Physics                │
                 │ Terrain                │
                 │ World                  │
                 └───────────┬────────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
            GLTF           CPU          GPU compiler
         / debugging      runtime            │
                                             ▼
                                     GFX906 runtime
                                             │
                              ┌──────────────┼──────────────┐
                              ▼              ▼              ▼
                           Compute        Raster        Memory
                              │              │              │
                              └──────────────┼──────────────┘
                                             ▼
                                          GFX906
```

Vulkan, RADV, ROCr/HSA, HIP, LLVM AMDGPU and AMDGPU/DRM are then **implementation layers to be investigated and selected according to what each workload actually requires**, rather than assumptions imposed at the top of the architecture.

---

# Guiding Principle

The eventual project should aim for:

> **Preserve the content. Reconstruct the semantics. Discard the historical runtime assumptions. Compile the recovered world into a runtime representation appropriate for the actual machine.**

The important separation is:

```text
WoW compatibility
        ≠
WoW's original implementation
        ≠
GLTF representation
        ≠
Vulkan representation
        ≠
GFX906 representation
```

Those should be independently transformable layers.