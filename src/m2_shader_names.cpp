#include "m2_shader_names.hpp"

#include <array>

namespace husk::m2 {

namespace {

struct ShaderEffectRow {
    const char* pixel;
    const char* vertex;
};

// s_modelShaderEffect, transcribed verbatim (pixel/vertex columns only --
// hull/domain/ff_colorOp/ff_alphaOp aren't needed for husk's glTF export)
// from documentation/wowdev-wiki/wikitext/M2/.skin.wiki's "Shader table"
// section (the pre-8.0.1 listing, NUM_M2SHADERS == 30 -- the 8.0.1 variant
// adds 4 more rows but that section of the wiki page doesn't give a full
// string array for its own vertex-shader table, so extending this isn't
// possible without guessing; see this file's own header doc comment).
constexpr std::array<ShaderEffectRow, 30> kShaderEffectTable = {{
    {"Combiners_Opaque_Mod2xNA_Alpha", "Diffuse_T1_Env"},
    {"Combiners_Opaque_AddAlpha", "Diffuse_T1_Env"},
    {"Combiners_Opaque_AddAlpha_Alpha", "Diffuse_T1_Env"},
    {"Combiners_Opaque_Mod2xNA_Alpha_Add", "Diffuse_T1_Env_T1"},
    {"Combiners_Mod_AddAlpha", "Diffuse_T1_Env"},
    {"Combiners_Opaque_AddAlpha", "Diffuse_T1_T1"},
    {"Combiners_Mod_AddAlpha", "Diffuse_T1_T1"},
    {"Combiners_Mod_AddAlpha_Alpha", "Diffuse_T1_Env"},
    {"Combiners_Opaque_Alpha_Alpha", "Diffuse_T1_Env"},
    {"Combiners_Opaque_Mod2xNA_Alpha_3s", "Diffuse_T1_Env_T1"},
    {"Combiners_Opaque_AddAlpha_Wgt", "Diffuse_T1_T1"},
    {"Combiners_Mod_Add_Alpha", "Diffuse_T1_Env"},
    {"Combiners_Opaque_ModNA_Alpha", "Diffuse_T1_Env"},
    {"Combiners_Mod_AddAlpha_Wgt", "Diffuse_T1_Env"},
    {"Combiners_Mod_AddAlpha_Wgt", "Diffuse_T1_T1"},
    {"Combiners_Opaque_AddAlpha_Wgt", "Diffuse_T1_T2"},
    {"Combiners_Opaque_Mod_Add_Wgt", "Diffuse_T1_Env"},
    {"Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", "Diffuse_T1_Env_T1"},
    {"Combiners_Mod_Dual_Crossfade", "Diffuse_T1_T1_T1"},
    {"Combiners_Mod_Depth", "Diffuse_EdgeFade_T1"},
    {"Combiners_Mod_AddAlpha_Alpha", "Diffuse_T1_Env_T2"},
    {"Combiners_Mod_Mod", "Diffuse_EdgeFade_T1_T2"},
    {"Combiners_Mod_Masked_Dual_Crossfade", "Diffuse_T1_T1_T1_T2"},
    {"Combiners_Opaque_Alpha", "Diffuse_T1_T1"},
    {"Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", "Diffuse_T1_Env_T2"},
    {"Combiners_Mod_Depth", "Diffuse_EdgeFade_Env"},
    {"Guild", "Diffuse_T1_T2_T1"},
    {"Guild_NoBorder", "Diffuse_T1_T2"},
    {"Guild_Opaque", "Diffuse_T1_T2_T1"},
    {"Illum", "Diffuse_T1_T1"},
}};

// M2GetPixelShaderID's low-15-bits formula (shaderId & 0x8000 clear),
// transcribed from the same wiki page.
std::string pixelShaderFromFormula(uint16_t shaderId, uint16_t opCount) {
    if (opCount == 1) {
        return (shaderId & 0x70) ? "Combiners_Mod" : "Combiners_Opaque";
    }
    unsigned lower = shaderId & 7;
    if (shaderId & 0x70) {
        switch (lower) {
            case 0: return "Combiners_Mod_Opaque";
            case 3: return "Combiners_Mod_Add";
            case 4: return "Combiners_Mod_Mod2x";
            case 6: return "Combiners_Mod_Mod2xNA";
            case 7: return "Combiners_Mod_AddNA";
            default: return "Combiners_Mod_Mod";
        }
    }
    switch (lower) {
        case 0: return "Combiners_Opaque_Opaque";
        case 3: return "Combiners_Opaque_AddAlpha";
        case 4: return "Combiners_Opaque_Mod2x";
        case 6: return "Combiners_Opaque_Mod2xNA";
        case 7: return "Combiners_Opaque_AddAlpha";
        default: return "Combiners_Opaque_Mod";
    }
}

// M2GetVertexShaderID's low-15-bits formula, same source.
std::string vertexShaderFromFormula(uint16_t shaderId, uint16_t opCount) {
    if (opCount == 1) {
        if (shaderId & 0x80) return "Diffuse_Env";
        return (shaderId & 0x4000) ? "Diffuse_T2" : "Diffuse_T1";
    }
    if (shaderId & 0x80) {
        return (shaderId & 0x8) ? "Diffuse_Env_Env" : "Diffuse_Env_T1";
    }
    if (shaderId & 0x8) return "Diffuse_T1_Env";
    return (shaderId & 0x4000) ? "Diffuse_T1_T2" : "Diffuse_T1_T1";
}

}  // namespace

ShaderNames resolveShaderNames(uint16_t shaderId, uint16_t opCount) {
    if (shaderId & 0x8000) {
        uint16_t index = shaderId & ~0x8000;
        if (index >= kShaderEffectTable.size()) {
            return ShaderNames{};  // resolved == false -- out of range, don't guess
        }
        const auto& row = kShaderEffectTable[index];
        return ShaderNames{true, row.pixel, row.vertex};
    }
    return ShaderNames{true, pixelShaderFromFormula(shaderId, opCount),
                        vertexShaderFromFormula(shaderId, opCount)};
}

}  // namespace husk::m2
