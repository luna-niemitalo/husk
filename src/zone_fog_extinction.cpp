#include "zone_fog_extinction.hpp"

#include <algorithm>

namespace husk::fog {

namespace {

// The stored density is in units of 1/2000 yard (WebWowViewerCpp
// MapSceneRenderer.cpp densityMultFix = 0.00050000002, the float 0.0005f as a
// decompiler prints it; wow.export's FogDataProvider.js copies it).
constexpr float kExtinctionPerDensity = 0.0005f;

// The client's floor on the main density (DayNightLightHolder.cpp,
// fmaxf(FogDensity, 0.89999998f)); it applies after the zone-light blend, so
// per key it is exact only where both blended keys are above it.
constexpr float kMinDensity = 0.9f;

// A key without a density gets one from its linear fog span, the classic
// power-fog exponent (noggit-red Sky.cpp _fog_rate uses the same 1.5 + 5.5
// ramp for 3.3.5). The client's span limit is min(far clip, 700) - 200
// yards; husk has no far clip, and every view distance setting of a modern
// client is past 700 yards, so the limit is taken as 500 (inferred).
constexpr float kFillSpanLimit = 700.0f - 200.0f;

float filledDensity(float fogEnd, float fogScaler) {
    float end = std::max(fogEnd, 10.0f);
    float scaler = std::clamp(fogScaler, -1.0f, 1.0f);
    float span = end - end * scaler;
    if (span > kFillSpanLimit || span <= 0.0f) return 1.5f;
    return (1.0f - span / kFillSpanLimit) * 5.5f + 1.5f;
}

}  // namespace

ZoneFogExtinction densityToExtinction(float fogDensity, float fogHeightDensity, float fogEnd, float fogScaler,
                                      float fogHeightScaler) {
    float density = fogDensity > 0.0f ? fogDensity : filledDensity(fogEnd, fogScaler);
    // A key without a height scaler fogs the height region like the rest
    // (DayNightLightHolder.cpp fixLightTimedData), taking the unfloored density.
    float heightDensity = fogHeightScaler == 0.0f ? density : fogHeightDensity;
    return {std::max(density, kMinDensity) * kExtinctionPerDensity, heightDensity * kExtinctionPerDensity};
}

}  // namespace husk::fog
