#pragma once

#include <cstdint>
#include <string>

namespace husk::m2 {

// Resolves M2Batch::shader_id + textureCount ("op_count") into the real
// {pixel, vertex} shader names the Cata+ client would pick -- transcribed
// directly from wowdev.wiki M2/.skin.wiki's decompiled M2GetPixelShaderID/
// M2GetVertexShaderID (Wow.exe build 12340) and its accompanying
// s_modelShaderEffect table, not derived or guessed. See
// TODO/MULTI_TEXTURE_LAYER_TODO.md for why this table -- not the WotLK-era
// blend-mode/op-count heuristic wowser's BatchManager implements -- is the
// one that applies to husk's own Legion+ scope ("this entire section only
// applies to selecting appropriate shaders for WotLK... it definitely stops
// applying from Cata and on").
struct ShaderNames {
    bool resolved = false;
    std::string pixel;
    std::string vertex;
};

// shaderId & 0x8000 set -> table lookup (s_modelShaderEffect); an
// out-of-range table index (>= NUM_M2SHADERS, 30 real rows transcribed
// below) resolves with `.resolved == false`, same "don't guess" policy as
// every other real-file-driven check in this project. shaderId & 0x8000
// clear -> the direct low-bits formula (always resolves; every input value
// is handled by the switch in the wiki's own decompiled function).
ShaderNames resolveShaderNames(uint16_t shaderId, uint16_t opCount);

}  // namespace husk::m2
