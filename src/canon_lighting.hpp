#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_material.hpp"  // TextureRef
#include "canon_primitives.hpp"
#include "canon_ref.hpp"
#include "canon_unresolved.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// The classic client's outdoor lighting: zone lights (Light rows, placed as
// classic_zone_light effect instances) pick a light parameter set per
// condition, and a set holds its sky, sun, ambient, cloud, water, fog and
// colour-grading values by time of day. A placement set lists each
// parameter set once (PlacementSet::lightParams); zones index it.
namespace husk::canon {

// A parameter set's fog at one time of day (LightData's fog columns; their
// roles in the client's fog record are candidates, EFFECTS.md).
struct ZoneFogValues {
    float end = 0;
    float scaler = 0;
    float density = 0;
    float startOffset = 0;
    float height = 0;
    float heightScaler = 0;
    float heightDensity = 0;
    float zScalar = 0;
    float curveStart = 0;
    float curveEnd = 0;
    float sunAngle = 0;  // a cosine
    float sunStrength = 0;
    float endColorDistance = 0;
    Vec3 skyColor;
    Vec3 endColor;
    Vec3 sunColor;
    Vec3 heightColor;
    Vec3 endHeightColor;
    std::array<float, 4> heightCoefficients{};
    std::array<float, 4> mainCoefficients{};
    std::array<float, 4> heightDensityCoefficients{};
};

// A parameter set's values at one time of day (one LightData row). Colours
// are RGB 0..1 as stored, no gamma decode.
struct LightKey {
    float time = 0;  // seconds since midnight
    Vec3 directColor;
    Vec3 ambientColor;
    Vec3 horizonAmbientColor;
    Vec3 groundAmbientColor;
    Vec3 skyTopColor;
    Vec3 skyMiddleColor;
    Vec3 skyBand1Color;
    Vec3 skyBand2Color;
    Vec3 skySmogColor;
    Vec3 sunColor;
    Vec3 cloudSunColor;
    Vec3 cloudEmissiveColor;
    Vec3 cloudLayer1AmbientColor;
    Vec3 cloudLayer2AmbientColor;
    float cloudDensity = 0;
    Vec3 oceanCloseColor;
    Vec3 oceanFarColor;
    Vec3 riverCloseColor;
    Vec3 riverFarColor;
    Vec3 shadowOpacity;  // stored as a colour; grey on every row seen
    // A 1024x32 lookup texture remapping the frame's colours (wowdev.wiki
    // DB/LightData); nullopt = none.
    std::optional<TextureRef> colorGrading;
    std::optional<TextureRef> darkerColorGrading;
    ZoneFogValues fog;
    // Colour high bytes (alpha or flags, unverified) and unnamed columns.
    std::vector<UnresolvedField> unresolved;
};

// LightSkybox: the sky model a parameter set draws.
struct Skybox {
    Ref ref;  // the LightSkybox row
    Ref model;
    std::optional<Ref> celestialModel;
    uint32_t flags = 0;  // wowdev.wiki DB/LightSkybox flag names, raw
};

// A parameter set's settings that don't change with time (one LightParams
// row).
struct LightSettings {
    bool highlightSky = false;
    std::optional<Skybox> skybox;
    uint32_t cloudType = 0;  // CloudType row; 0 = none (every 12.1.0 row)
    float glow = 0;          // fog added to everything (wowdev.wiki DB/LightParams)
    float riverShallowAlpha = 0;
    float riverDeepAlpha = 0;
    float oceanShallowAlpha = 0;
    float oceanDeepAlpha = 0;
    uint32_t flags = 0;  // raw; hide sun/moon/stars are bits 0x4/0x8/0x10
    uint32_t ssaoSettings = 0;  // SSAOSettings row; 0 = none
    // Radians, from the degrees stored (90, 135, 225, -90; one 6300).
    float sunPolar = 0;
    float sunAzimuth = 0;
    float sunAttenuationStart = 0;
    float sunAttenuationEnd = 0;
    std::optional<Vec3> celestialSphereOverride;  // set when stored nonzero
    std::optional<Vec3> sunPositionOverride;
    std::vector<UnresolvedField> unresolved;
};

struct LightParams {
    Ref ref;  // the LightParams row
    // nullopt: the build's LightParams file has no row for this id, though
    // LightData keys it (1,196 of 2,377 used ids in 12.1.0).
    std::optional<LightSettings> settings;
    std::vector<LightKey> keys;  // ascending time
};

}  // namespace husk::canon
