#include "char_composite.hpp"

#include <algorithm>
#include <cstdint>

namespace husk::char_composite {

namespace {

// Nearest-neighbor resample, matching the real client's own
// texParameteri(TEXTURE_MIN_FILTER, NEAREST) for the layer's own source
// texture -- see char_composite.hpp's module doc comment.
blp::Image resample(const blp::Image& src, uint32_t dstWidth, uint32_t dstHeight) {
    blp::Image out;
    out.width = dstWidth;
    out.height = dstHeight;
    out.rgba.resize(static_cast<size_t>(dstWidth) * dstHeight * 4);
    for (uint32_t dy = 0; dy < dstHeight; ++dy) {
        uint32_t sy = std::min(src.height - 1, static_cast<uint32_t>(
                                                    static_cast<uint64_t>(dy) * src.height / dstHeight));
        for (uint32_t dx = 0; dx < dstWidth; ++dx) {
            uint32_t sx = std::min(
                src.width - 1, static_cast<uint32_t>(static_cast<uint64_t>(dx) * src.width / dstWidth));
            const uint8_t* s = &src.rgba[(static_cast<size_t>(sy) * src.width + sx) * 4];
            uint8_t* d = &out.rgba[(static_cast<size_t>(dy) * dstWidth + dx) * 4];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }
    return out;
}

struct Rgba {
    float r, g, b, a;
};

Rgba toFloat(const uint8_t* p) { return {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f}; }

void writeFloat(uint8_t* p, const Rgba& c) {
    auto clamp = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    p[0] = clamp(c.r);
    p[1] = clamp(c.g);
    p[2] = clamp(c.b);
    p[3] = clamp(c.a);
}

float overlayChannel(float base, float blend) {
    return (blend < 0.5f) ? (2.0f * base * blend) : (1.0f - 2.0f * (1.0f - base) * (1.0f - blend));
}

bool isSupportedBlendMode(uint32_t mode) {
    return mode == 0 || mode == 1 || mode == 4 || mode == 6 || mode == 7 || mode == 9 || mode == 15;
}

}  // namespace

blp::Image composite(std::vector<Layer> layers, uint32_t atlasWidth, uint32_t atlasHeight, std::ostream& err) {
    blp::Image canvas;
    canvas.width = atlasWidth;
    canvas.height = atlasHeight;
    canvas.rgba.assign(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);
    for (size_t i = 3; i < canvas.rgba.size(); i += 4) canvas.rgba[i] = 255;  // opaque black, matches
                                                                               // gl.clearColor(0,0,0,1)

    std::sort(layers.begin(), layers.end(),
              [](const Layer& a, const Layer& b) { return a.targetId < b.targetId; });

    for (const Layer& layer : layers) {
        if (layer.width == 0 || layer.height == 0) {
            err << "husk: note: char_composite: target " << layer.targetId
                << " has a zero-size rect -- skipping\n";
            continue;
        }
        if (static_cast<uint64_t>(layer.x) + layer.width > atlasWidth ||
            static_cast<uint64_t>(layer.y) + layer.height > atlasHeight) {
            err << "husk: note: char_composite: target " << layer.targetId << "'s rect (" << layer.x << ","
                << layer.y << " " << layer.width << "x" << layer.height << ") doesn't fit the " << atlasWidth
                << "x" << atlasHeight << " atlas -- skipping\n";
            continue;
        }
        if (!isSupportedBlendMode(layer.blendMode)) {
            err << "husk: note: char_composite: target " << layer.targetId << " uses BlendMode "
                << layer.blendMode
                << ", not one of the real, verified set {0,1,4,6,7,9,15} -- skipping rather than "
                   "guessing (see char_composite.hpp's module doc comment)\n";
            continue;
        }
        if (layer.image.width == 0 || layer.image.height == 0) {
            err << "husk: note: char_composite: target " << layer.targetId
                << "'s source image is empty -- skipping\n";
            continue;
        }

        blp::Image blend = resample(layer.image, layer.width, layer.height);

        for (uint32_t ry = 0; ry < layer.height; ++ry) {
            uint32_t cy = layer.y + ry;
            for (uint32_t rx = 0; rx < layer.width; ++rx) {
                uint32_t cx = layer.x + rx;
                uint8_t* dst = &canvas.rgba[(static_cast<size_t>(cy) * atlasWidth + cx) * 4];
                const uint8_t* blendPx = &blend.rgba[(static_cast<size_t>(ry) * layer.width + rx) * 4];

                if (layer.blendMode == 0 || layer.blendMode == 1) {
                    dst[0] = blendPx[0];
                    dst[1] = blendPx[1];
                    dst[2] = blendPx[2];
                    dst[3] = blendPx[3];
                    continue;
                }

                Rgba base = toFloat(dst);
                Rgba blendC = toFloat(blendPx);
                Rgba fragment;
                switch (layer.blendMode) {
                    case 4:  // MULTIPLY -- gl_FragColor = base * blend, full vec4
                        fragment = {base.r * blendC.r, base.g * blendC.g, base.b * blendC.b,
                                    base.a * blendC.a};
                        break;
                    case 6:  // OVERLAY
                        fragment = {overlayChannel(base.r, blendC.r), overlayChannel(base.g, blendC.g),
                                    overlayChannel(base.b, blendC.b), blendC.a};
                        break;
                    case 7:  // SCREEN
                        fragment = {1.0f - (1.0f - base.r) * (1.0f - blendC.r),
                                    1.0f - (1.0f - base.g) * (1.0f - blendC.g),
                                    1.0f - (1.0f - base.b) * (1.0f - blendC.b), blendC.a};
                        break;
                    default:  // 9 (Alpha Straight) / 15 (Infer alpha blend): direct source copy
                        fragment = blendC;
                        break;
                }

                float srcA = fragment.a;
                float outA;
                if (layer.blendMode == 9) {
                    // blendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA) --
                    // alpha channel's own src factor is ONE, not SRC_ALPHA.
                    outA = srcA + base.a * (1.0f - srcA);
                } else {
                    // Standard blendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA) applies the same
                    // factors to every channel including alpha (a real GL default-blendFunc
                    // property, not a simplification) -- so the alpha channel itself also
                    // gets multiplied by srcA once more here.
                    outA = srcA * srcA + base.a * (1.0f - srcA);
                }
                Rgba result = {fragment.r * srcA + base.r * (1.0f - srcA),
                                fragment.g * srcA + base.g * (1.0f - srcA),
                                fragment.b * srcA + base.b * (1.0f - srcA), outA};
                writeFloat(dst, result);
            }
        }
    }

    return canvas;
}

}  // namespace husk::char_composite
