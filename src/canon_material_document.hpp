#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "canon_material.hpp"  // TextureRef, UvRef, FramebufferBlend
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// A material document (REFACTOR/MATERIALS.md): surface layers over textures
// and inputs, each combined with a named function, so a renderer can draw
// the material with nothing else. A texture may be a composite: other
// textures drawn onto a canvas (a character's skin atlas).
namespace husk::canon {

// A texture a layer draws: an index into MaterialDocument::textures, or the
// name of an image input a user of the material binds.
using TextureSource = std::variant<uint32_t, std::string>;

// The client compositor's operations, in its own case order
// (texturecompositing.bls); schema/material.schema.json states each one's
// formula.
enum class CompositeBlend {
    None, Copy, CopyAlphaTested, Add, Multiply, Multiply2x, Overlay, Screen, HardLight,
    AlphaBlend, MultiplyOpaque, MultiplyRedAlpha, Greyscale, GradientMap
};

struct CompositeLayer {
    TextureSource texture;
    std::array<uint32_t, 4> rect{};  // x, y, width, height from the canvas's top left
    std::optional<CompositeBlend> blend;  // nullopt when the source's mode has no name
    uint32_t sourceBlendMode = 0;
    uint32_t sourceLayer = 0;
};

struct CompositeTexture {
    uint32_t width = 0;
    uint32_t height = 0;
    // Whether each layer's blend is known to be what its source mode selects.
    bool blendMappingVerified = false;
    std::vector<CompositeLayer> layers;  // drawn in order
};

using MaterialTexture = std::variant<TextureRef, CompositeTexture>;

enum class SurfaceRole { Base, Emission, Specular, Reflection, Mask, Detail };
enum class Combine { Replace, Modulate, Modulate2x, Add, AlphaBlend };

struct SurfaceLayer {
    SurfaceRole role = SurfaceRole::Base;
    TextureSource texture;
    UvRef uv;
    Combine combine = Combine::Replace;
};

// An optional image input a user of the material binds.
struct ImageInput {
    std::string name;
    std::string description;
};

struct MaterialDocument {
    Ref subject;
    std::vector<ImageInput> imageInputs;
    std::vector<MaterialTexture> textures;
    std::vector<SurfaceLayer> layers;  // bottom first
    std::optional<FramebufferBlend> framebufferBlend;
    bool lit = true;
};

}  // namespace husk::canon
