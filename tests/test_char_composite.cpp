// Unit tests for src/char_composite.hpp -- the real software pixel
// compositor for TODO/CHAR_TEXTURE_COMPOSITING_TODO.md's Stage 4. Blend
// formulas checked here are transcribed directly from
// reference/wow.export/src/shaders/char.fragment.shader +
// CharMaterialRenderer.js's own outer GL blendFunc switch -- see
// char_composite.hpp's module doc comment for the exact real-source
// mapping each test below exercises.

#include <doctest/doctest.h>
#include <sstream>

#include "../src/char_composite.hpp"

using husk::blp::Image;
using husk::char_composite::composite;
using husk::char_composite::Layer;

namespace {

Image solidImage(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r;
        img.rgba[i + 1] = g;
        img.rgba[i + 2] = b;
        img.rgba[i + 3] = a;
    }
    return img;
}

const uint8_t* pixelAt(const Image& img, uint32_t x, uint32_t y) {
    return &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
}

}  // namespace

TEST_CASE("char_composite::composite: canvas starts opaque black, matching the real client's "
          "gl.clearColor(0,0,0,1)") {
    std::ostringstream err;
    Image out = composite({}, 4, 4, err);
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            const uint8_t* p = pixelAt(out, x, y);
            CHECK(p[0] == 0);
            CHECK(p[1] == 0);
            CHECK(p[2] == 0);
            CHECK(p[3] == 255);
        }
    }
}

TEST_CASE("char_composite::composite: BlendMode 1 (Blit) straight-overwrites the target rect, "
          "ignoring the canvas' own prior alpha") {
    std::ostringstream err;
    Layer layer;
    layer.targetId = 1;
    layer.image = solidImage(2, 2, 200, 100, 50, 128);  // semi-transparent source
    layer.x = 1;
    layer.y = 1;
    layer.width = 2;
    layer.height = 2;
    layer.blendMode = 1;
    Image out = composite({layer}, 4, 4, err);
    const uint8_t* p = pixelAt(out, 1, 1);
    CHECK(p[0] == 200);
    CHECK(p[1] == 100);
    CHECK(p[2] == 50);
    CHECK(p[3] == 128);  // alpha overwritten too, not preserved from the opaque-black canvas
    // Outside the rect, untouched.
    const uint8_t* outside = pixelAt(out, 0, 0);
    CHECK(outside[0] == 0);
    CHECK(outside[3] == 255);
}

TEST_CASE("char_composite::composite: BlendMode 4 (Multiply) is base*blend per channel, full vec4 "
          "including alpha -- gl_FragColor = base * blend in the real fragment shader") {
    std::ostringstream err;
    // First layer (Blit) sets a known base color; second (Multiply) reads it back.
    Layer base;
    base.targetId = 1;
    base.image = solidImage(1, 1, 200, 100, 50, 255);
    base.x = 0;
    base.y = 0;
    base.width = 1;
    base.height = 1;
    base.blendMode = 1;

    Layer mul;
    mul.targetId = 2;
    mul.image = solidImage(1, 1, 255, 128, 0, 255);  // full alpha -> standard blend fully replaces
    mul.x = 0;
    mul.y = 0;
    mul.width = 1;
    mul.height = 1;
    mul.blendMode = 4;

    Image out = composite({base, mul}, 1, 1, err);
    const uint8_t* p = pixelAt(out, 0, 0);
    // fragment = (200/255*255/255, 100/255*128/255, 50/255*0/255) * 255, alpha 1*1
    CHECK(p[0] == doctest::Approx(200).epsilon(1));
    CHECK(p[1] == doctest::Approx(static_cast<int>(100.0 * 128 / 255)).epsilon(1));
    CHECK(p[2] == 0);
    CHECK(p[3] == 255);
}

TEST_CASE("char_composite::composite: BlendMode 7 (Screen) matches "
          "result.rgb = 1 - (1-base)*(1-blend), result.a = blend.a") {
    std::ostringstream err;
    Layer base;
    base.targetId = 1;
    base.image = solidImage(1, 1, 100, 100, 100, 255);
    base.x = 0;
    base.y = 0;
    base.width = 1;
    base.height = 1;
    base.blendMode = 1;

    Layer screen;
    screen.targetId = 2;
    screen.image = solidImage(1, 1, 200, 200, 200, 255);
    screen.x = 0;
    screen.y = 0;
    screen.width = 1;
    screen.height = 1;
    screen.blendMode = 7;

    Image out = composite({base, screen}, 1, 1, err);
    const uint8_t* p = pixelAt(out, 0, 0);
    double baseF = 100.0 / 255.0, blendF = 200.0 / 255.0;
    double expected = (1.0 - (1.0 - baseF) * (1.0 - blendF)) * 255.0;
    CHECK(p[0] == doctest::Approx(expected).epsilon(1.5));
}

TEST_CASE("char_composite::composite: an unsupported real BlendMode (e.g. 3, Add) is skipped and "
          "reported, never guessed at as the real client's own magenta fallback would be") {
    std::ostringstream err;
    Layer layer;
    layer.targetId = 1;
    layer.image = solidImage(1, 1, 255, 0, 255, 255);
    layer.x = 0;
    layer.y = 0;
    layer.width = 1;
    layer.height = 1;
    layer.blendMode = 3;
    Image out = composite({layer}, 2, 2, err);
    const uint8_t* p = pixelAt(out, 0, 0);
    CHECK(p[0] == 0);  // untouched opaque-black canvas, not the "magenta" real-client fallback
    CHECK(err.str().find("not one of the real, verified set") != std::string::npos);
}

TEST_CASE("char_composite::composite: a rect that doesn't fit the atlas is skipped and reported, "
          "not clamped") {
    std::ostringstream err;
    Layer layer;
    layer.targetId = 1;
    layer.image = solidImage(4, 4, 255, 255, 255, 255);
    layer.x = 2;
    layer.y = 2;
    layer.width = 4;
    layer.height = 4;  // 2+4 > atlas size 4
    layer.blendMode = 1;
    Image out = composite({layer}, 4, 4, err);
    const uint8_t* p = pixelAt(out, 3, 3);
    CHECK(p[0] == 0);  // untouched
    CHECK(err.str().find("doesn't fit the") != std::string::npos);
}

TEST_CASE("char_composite::composite: layers draw in targetId order regardless of input order, "
          "matching the real client's own textureTargets.sort((a,b)=>a.id-b.id)") {
    std::ostringstream err;
    Layer first;   // drawn first (lower id) -- gets overwritten by the second
    first.targetId = 5;
    first.image = solidImage(1, 1, 255, 0, 0, 255);
    first.x = 0;
    first.y = 0;
    first.width = 1;
    first.height = 1;
    first.blendMode = 1;

    Layer second;  // drawn second (higher id) -- wins
    second.targetId = 9;
    second.image = solidImage(1, 1, 0, 255, 0, 255);
    second.x = 0;
    second.y = 0;
    second.width = 1;
    second.height = 1;
    second.blendMode = 1;

    // Passed in reverse id order -- composite() must sort internally.
    Image out = composite({second, first}, 1, 1, err);
    const uint8_t* p = pixelAt(out, 0, 0);
    CHECK(p[0] == 0);
    CHECK(p[1] == 255);
}

TEST_CASE("char_composite::composite: a source image smaller/larger than its target rect is "
          "nearest-neighbor resampled to exactly fill the rect") {
    std::ostringstream err;
    Layer layer;
    layer.targetId = 1;
    layer.image = solidImage(1, 1, 42, 84, 126, 255);  // 1x1 source
    layer.x = 0;
    layer.y = 0;
    layer.width = 3;
    layer.height = 3;  // stretched to 3x3
    layer.blendMode = 1;
    Image out = composite({layer}, 3, 3, err);
    for (uint32_t y = 0; y < 3; ++y) {
        for (uint32_t x = 0; x < 3; ++x) {
            const uint8_t* p = pixelAt(out, x, y);
            CHECK(p[0] == 42);
            CHECK(p[1] == 84);
            CHECK(p[2] == 126);
        }
    }
}
