#pragma once

// Derivation rule fog_density_to_extinction (REFACTOR/EFFECTS.md "Derivation
// rules"): LightData's FogDensity / FogHeightDensity -> the extinction per
// yard the fog shader applies past the fog start, vis = exp(-e * k)
// (SHADER_FINDINGS/notes/fog.md, cb5 [0].z and [4].x).
//
// The CPU transform is not in any shader, wiki page or DB2 field. It rests on
// one reimplementation, Deamon87's WebWowViewerCpp (decompiler-printed
// constants, no stated citation); evidence and grades per step are in
// WIKI_FINDINGS/RENDERING.md "Zone fog density".
namespace husk::fog {

struct ZoneFogExtinction {
    float main = 0;    // 1/yard, cb5 [0].z
    float height = 0;  // 1/yard, cb5 [4].x
};

// IN: one LightData row's raw columns, as stored. Non-critical: the result is
// a derived field, the raw values stay in the bundle under `source`.
ZoneFogExtinction densityToExtinction(float fogDensity, float fogHeightDensity, float fogEnd, float fogScaler,
                                      float fogHeightScaler);

}  // namespace husk::fog
