#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "canon_curve.hpp"
#include "canon_material.hpp"  // TextureRef, FramebufferBlend
#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's Resources layer, the non-mesh things a model places in
// the world: attachment points, animation event markers, lights, ribbon and
// particle emitters.
//
// Every `position` is a point in the model's own space (the same frame as
// canon::Mesh::positions), carried by `bone`'s animation -- NOT an offset from
// the bone. Checked on real files: every attachment of creature/fox/fox.m2 and
// every emitter of sword_1h_artifactskywall_d_06.m2 sits exactly on its bone's
// pivot.
namespace husk::canon {

// One property's whole animation: one curve per sequence or global sequence
// that has keyframes for it, in source order. Empty means "never animated",
// and a consumer then has no value at all for it -- M2 stores no separate
// static default for these fields.
template <typename T>
using Animated = std::vector<Curve<T>>;

// A particle-lifetime curve (M2 FBlock): keyed by the particle's own age, not
// by any sequence. `timestamp` is the raw on-disk u16 -- real data runs 0 to
// 0x7FFF and reads as a lifetime fraction, but no source confirms the scale,
// so it is carried unscaled (m2::FBlockMeta).
template <typename T>
struct LifetimeCurve {
    std::vector<std::pair<uint16_t, T>> keyframes;
};

struct Attachment {
    Ref ref;           // id = RecordIndex{position in Scene::attachments}; name = the attachment-point kind's wiki name, Synthesized
    uint32_t pointId = 0;  // M2Attachment::id, the attachment-point kind (wowdev.wiki M2#Attachments table)
    Ref bone;
    Vec3 position;
    Animated<float> animateAttached;  // 0/1: whether a model attached here animates with this one
};

// M2Event. Its firing times (the event's own timestamp track) are not parsed,
// so this is placement only.
struct Event {
    std::string identifier;  // e.g. "$DTH"
    uint32_t data = 0;       // passed to the event handler; meaning varies per identifier
    Ref bone;
    Vec3 position;
};

struct Light {
    uint16_t type = 0;  // M2Light::type: 0 directional, 1 point (wowdev.wiki M2#Lights)
    std::optional<Ref> bone;  // nullopt: not attached to a bone
    Vec3 position;
    Animated<Vec3> ambientColor;  // RGB 0..1
    Animated<float> ambientIntensity;
    Animated<Vec3> diffuseColor;
    Animated<float> diffuseIntensity;
    Animated<float> attenuationStart;
    Animated<float> attenuationEnd;
    Animated<float> visibility;  // 0/1
};

// One M2Material a ribbon draws with: render flags plus framebuffer blend.
struct RenderState {
    uint16_t flags = 0;  // M2Material::flags, raw (wowdev.wiki M2#Render_flags)
    std::optional<FramebufferBlend> blend;  // nullopt when the source mode is outside M2BLEND's 0..7
};

struct RibbonEmitter {
    uint32_t ribbonId = 0;
    Ref bone;
    Vec3 position;
    std::vector<TextureRef> textures;
    std::vector<RenderState> materials;
    Animated<Vec3> color;  // RGB 0..1
    Animated<float> alpha;  // 0..1
    Animated<float> heightAbove;
    Animated<float> heightBelow;
    Animated<float> textureSlot;  // index into the ribbon's texture grid
    Animated<float> visibility;   // 0/1
    float edgesPerSecond = 0;
    float edgeLifetime = 0;  // seconds
    float gravity = 0;
    uint16_t textureRows = 0;
    uint16_t textureColumns = 0;
    int16_t priorityPlane = 0;
    int8_t ribbonColorIndex = 0;
    // Index into the source model's texture-transform lookup table -- the
    // ribbon's UV animation is not resolved into curves yet.
    int8_t textureTransformLookupIndex = 0;
};

struct ParticleEmitter {
    uint32_t particleId = 0;
    uint32_t flags = 0;  // M2Particle flags, raw (wowdev.wiki M2#Particle_Flags)
    Ref bone;
    Vec3 position;
    // One texture, or up to three for a MultiTexture (flags 0x10000000)
    // emitter, in layer order.
    std::vector<TextureRef> textures;
    std::optional<Ref> particleModel;       // set: spawns model particles (a game path, M2Embedded)
    std::optional<Ref> childEmittersModel;  // set: child emitters come from this model
    uint8_t blendingType = 0;  // wowdev.wiki M2#Particle_Blendings, raw
    uint8_t emitterType = 0;   // 1 plane, 2 sphere, 3 spline, 4 bone
    uint16_t particleColorIndex = 0;  // ParticleColor.db2 selector, 0 = unmodified
    float multiTextureScale[2] = {0, 0};
    int16_t priorityPlane = 0;
    uint16_t textureRows = 0;
    uint16_t textureColumns = 0;

    Animated<float> emissionSpeed;
    Animated<float> speedVariation;
    Animated<float> verticalRange;
    Animated<float> horizontalRange;
    Animated<float> gravity;
    Animated<float> lifespan;
    Animated<float> emissionRate;
    Animated<float> emissionAreaLength;
    Animated<float> emissionAreaWidth;
    Animated<float> zSource;
    Animated<float> enabledIn;  // 0/1
    float lifespanVariation = 0;
    float emissionRateVariation = 0;

    LifetimeCurve<Vec3> color;  // RGB, on-disk scale (observed 0..255), not normalized
    LifetimeCurve<float> alpha;  // 0..1
    LifetimeCurve<Vec2> scale;
    LifetimeCurve<uint16_t> headCell;  // flipbook cell index
    LifetimeCurve<uint16_t> tailCell;
    Vec2 scaleVariation;

    float tailLength = 0;
    float twinkleSpeed = 0;
    float twinklePercent = 0;
    float twinkleScaleMin = 0;
    float twinkleScaleMax = 0;
    float inheritVelocityScale = 0;
    float drag = 0;
    float baseSpin = 0;
    float baseSpinVariation = 0;
    float spinSpeed = 0;
    float spinSpeedVariation = 0;
    Vec3 tumbleMin;
    Vec3 tumbleMax;
    Vec3 windVector;
    float windTime = 0;
    float followSpeed1 = 0;
    float followScale1 = 0;
    float followSpeed2 = 0;
    float followScale2 = 0;
    std::vector<Vec3> splinePoints;
    float multiTextureScrollMid[4] = {0, 0, 0, 0};    // {x0, y0, x1, y1}
    float multiTextureScrollRange[4] = {0, 0, 0, 0};
};

// A static light the model places (WMO MNLD and MOLT): no bone, no
// keyframes. Flicker is a procedural runtime effect the source describes by
// parameters only.
struct LocalLight {
    enum class Kind { Point, Spot, Directional, Ambient };
    Ref ref;  // id = RecordIndex in the scene's list; name synthesized; `origin` names the source record
    Kind kind = Kind::Point;
    Vec3 position;
    // At most one is set. MOLT stores a quaternion (spot/directional only);
    // MNLD stores Euler radians in an undocumented axis order, carried raw.
    std::optional<Quat> rotation;
    std::optional<Vec3> eulerRotation;
    Vec3 color;  // RGB 0..1 as stored, no gamma decode; MNLD's inner colour
    float intensity = 0;
    float intensityMultiplier = 1;  // MNLD; a stored 0 already reads as 1
    bool attenuated = true;         // MOLT useAtten
    float attenuationStart = 0;
    float attenuationEnd = 0;

    // MNLD flag 0x1: colour blends from `color` at blendStart to this at
    // blendEnd (distances from the light).
    struct Gradient {
        Vec3 outerColor;
        float blendStart = 0;
        float blendEnd = 0;
    };
    std::optional<Gradient> gradient;

    struct Cone {
        float falloffExponent = 0;
        float innerAngle = 0;  // radians; whole-cone or half-angle is unverified (real values reach 2.2)
        float outerAngle = 0;
    };
    std::optional<Cone> cone;  // Kind::Spot from MNLD

    struct Flicker {
        uint32_t mode = 0;  // 1 sine, 2 noise, 3 noise step
        float intensity = 0;
        float speed = 0;
    };
    std::optional<Flicker> flicker;  // nullopt = steady

    bool castsShadows = false;
    float shadowScale = 0;  // MNLD scale, meaningful with castsShadows
    std::optional<Ref> cookie;  // projected texture (FileDataID); a cube map for point lights
    // The owned placement set this light switches with (MNLD doodadSet);
    // nullopt = always on with the model.
    std::optional<Ref> placementSet;
    uint32_t sourceFlags = 0;  // MNLD flags or MOLT's two unknown bytes, raw
    SourceRecord origin;
};

// A box the client fills with ambient particles (WMO MPVD: dust, snow,
// ash), in model space. Layout: WIKI_FINDINGS/WORLD.md "WMO MPVD"; names
// here are value-characterized from the corpus, not from any symbol, and
// fields with no characterization at all stay in `sourceRecord`.
struct ParticulateVolume {
    Ref ref;  // id = RecordIndex in the scene's list; name synthesized; `origin` names the source record
    Vec3 position;
    Vec3 extent;  // box size per axis; full size or half size unverified
    Vec3 eulerRotation;  // radians, axis order unverified
    uint32_t presetId = 0;       // shared across unrelated files; what it names is unresolved
    uint32_t particleCount = 0;  // count/density-like
    float opacity = 0;           // density/opacity-like, 0..1

    // An emission envelope: value pulses over time (seconds, ascending).
    struct EnvelopeKey {
        float time = 0;
        float value = 0;
        float value2 = 0;
    };
    std::vector<EnvelopeKey> envelope;  // empty = constant

    struct Emitter {
        uint32_t slot = 0;  // position in the source record's emitter array
        TextureRef texture;
        std::optional<FramebufferBlend> blend;  // nullopt when the source mode has no canon name
        uint32_t sourceBlend = 0;               // raw EGxBlend
        std::array<float, 4> color{};           // RGBA 0..1
        uint32_t tilesX = 0;                    // flipbook grid
        uint32_t tilesY = 0;
        uint32_t frames = 0;  // flipbook frame count; may use fewer than tilesX * tilesY tiles
        std::array<float, 32> parameters{};  // emission parameters, per-field semantics unresolved
    };
    std::vector<Emitter> emitters;

    uint32_t sourceFlags = 0;
    std::vector<uint8_t> sourceRecord;  // the whole source record, raw
    SourceRecord origin;
};

// A region that overrides the model's ambient colour (WMO MAVG global,
// MAVD point, MBVD box; 8.3+). How the client picks among them and which
// colours flag 0x1 selects are unverified on the wiki (and contradictory
// for the flag), so all three colours and the flags stay raw.
struct AmbientVolume {
    enum class Shape { Global, Point, Box };
    Ref ref;  // id = RecordIndex in the scene's list; name synthesized; `origin` names the source record
    Shape shape = Shape::Global;
    Vec3 position;   // Point
    float start = 0;  // Point: distances from position, start <= end
    float end = 0;    // Point; Box: fade distance in from the faces (inferred)

    // Box: six outward planes in opposite pairs; inside where
    // dot(normal, p) <= distance for all six.
    struct Plane {
        Vec3 normal;
        float distance = 0;
    };
    std::vector<Plane> planes;

    std::array<std::array<float, 4>, 3> colors{};  // RGBA 0..1 as stored, color1..color3
    uint32_t sourceFlags = 0;
    SourceRecord origin;
    // The owned placement set this volume applies with (its doodadSetId).
    std::optional<Ref> placementSet;
};

// A fog the model places (WMO MFOG), model space. Per the wiki: fog
// thickens from `end * startScalar` to `end` (distances from the camera),
// inside a sphere that fades between the two radii. Parts name the fogs
// that apply to them (MeshPart::fogs).
struct LocalFog {
    Ref ref;  // id = RecordIndex in the scene's list; name synthesized; `origin` names the source record
    Vec3 position;
    float innerRadius = 0;  // source smaller_radius
    float outerRadius = 0;  // source larger_radius
    // Source flag 0x1 (wiki F_IEBLEND): radius ignored; not drawn, but
    // scales the area fog down near the model's exits.
    bool interiorExteriorBlend = false;
    struct Layer {
        float end = 0;
        float startScalar = 0;
        std::array<float, 4> color{};  // RGBA 0..1 as stored
    };
    Layer fog;
    Layer underwater;
    uint32_t sourceFlags = 0;
    SourceRecord origin;
    // The owned placement set this fog applies with (MFED doodadSetId);
    // nullopt when the source has no MFED.
    std::optional<Ref> placementSet;
};

struct Scene {
    std::vector<Attachment> attachments;
    std::vector<Event> events;
    std::vector<Light> lights;
    std::vector<LocalLight> localLights;
    std::vector<ParticulateVolume> particulateVolumes;
    std::vector<AmbientVolume> ambientVolumes;
    std::vector<LocalFog> fogs;
    std::vector<RibbonEmitter> ribbons;
    std::vector<ParticleEmitter> particles;
};

}  // namespace husk::canon
