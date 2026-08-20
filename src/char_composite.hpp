#pragma once

#include <cstdint>
#include <ostream>
#include <vector>

#include "blp.hpp"

// Real software pixel compositor for TODO/CHAR_TEXTURE_COMPOSITING_TODO.md's
// Stage 4 -- given an ordered set of resolved texture layers (real
// FileDataID -> decoded pixels, a real placement rect, a real blend mode),
// produces one composited base-atlas image, same math the real client's
// WebGL renderer computes (reference/wow.export/src/js/3D/renderers/
// CharMaterialRenderer.js + src/shaders/char.fragment.shader), just run in
// software instead of on a GPU (husk has no GPU-shader dependency and
// shouldn't gain one for this, per this project's own DESIGN.md).
//
// Real blend-mode mapping, transcribed directly from the two files above,
// not guessed at:
//   0 (None) / 1 (Blit)        -- straight overwrite, no blending at all
//                                  (real client: gl.disable(BLEND))
//   15 (Infer alpha blend)     -- straight copy of the source pixel,
//                                  standard alpha-over onto the canvas
//   9  (Alpha Straight)        -- same source pixel as 15, but a different
//                                  real alpha-accumulation formula (no
//                                  src_alpha^2 term -- see composite()'s own
//                                  comment for the exact GL blendFuncSeparate
//                                  this reproduces)
//   4  (Multiply)              -- result = base * blend (base = the canvas'
//                                  own current pixels in the target rect,
//                                  read back before this layer draws, same
//                                  as the real client's own u_baseTexture)
//   6  (Overlay) / 7 (Screen)  -- real per-channel formulas, see
//                                  composite()'s implementation
// Every other real BlendMode value (2,3,5,8,10-14, or anything undocumented)
// is refused, not guessed at -- the real client's own fragment shader falls
// back to a solid magenta debug square for these ("poke a dev", per its own
// source comment), which isn't something worth reproducing as real output.
namespace husk::char_composite {

struct Layer {
    uint32_t targetId = 0;  // ChrModelTextureTargetID -- real draw order, ascending
    blp::Image image;       // decoded source texture, native resolution
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;   // target rect size within the atlas -- the source image is
    uint32_t height = 0;  // resampled (nearest-neighbor) to exactly this size first
    uint32_t blendMode = 0;
};

// Composites `layers` (sorted internally by targetId ascending, matching the
// real client's own `this.textureTargets.sort((a,b)=>a.id-b.id)`) onto an
// atlasWidth x atlasHeight canvas starting from opaque black (matches the
// real client's gl.clearColor(0,0,0,1)). A layer whose rect falls partly or
// fully outside the atlas, or whose blend mode isn't one of the real,
// verified set above, is reported to `err` and skipped -- never silently
// clamped or guessed at.
blp::Image composite(std::vector<Layer> layers, uint32_t atlasWidth, uint32_t atlasHeight, std::ostream& err);

}  // namespace husk::char_composite
