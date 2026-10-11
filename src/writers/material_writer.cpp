#include "writers/material_writer.hpp"

#include <sstream>
#include <variant>

#include "json_writer.hpp"

namespace husk::writers {

namespace {

const char* compositeBlendName(canon::CompositeBlend blend) {
    switch (blend) {
        case canon::CompositeBlend::None: return "none";
        case canon::CompositeBlend::Copy: return "copy";
        case canon::CompositeBlend::CopyAlphaTested: return "copy_alpha_tested";
        case canon::CompositeBlend::Add: return "add";
        case canon::CompositeBlend::Multiply: return "multiply";
        case canon::CompositeBlend::Multiply2x: return "multiply_2x";
        case canon::CompositeBlend::Overlay: return "overlay";
        case canon::CompositeBlend::Screen: return "screen";
        case canon::CompositeBlend::HardLight: return "hard_light";
        case canon::CompositeBlend::AlphaBlend: return "alpha_blend";
        case canon::CompositeBlend::MultiplyOpaque: return "multiply_opaque";
        case canon::CompositeBlend::MultiplyRedAlpha: return "multiply_red_alpha";
        case canon::CompositeBlend::Greyscale: return "greyscale";
        case canon::CompositeBlend::GradientMap: return "gradient_map";
    }
    return "none";
}

const char* roleName(canon::SurfaceRole role) {
    switch (role) {
        case canon::SurfaceRole::Base: return "base";
        case canon::SurfaceRole::Emission: return "emission";
        case canon::SurfaceRole::Specular: return "specular";
        case canon::SurfaceRole::Reflection: return "reflection";
        case canon::SurfaceRole::Mask: return "mask";
        case canon::SurfaceRole::Detail: return "detail";
    }
    return "base";
}

const char* combineName(canon::Combine combine) {
    switch (combine) {
        case canon::Combine::Replace: return "replace";
        case canon::Combine::Modulate: return "modulate";
        case canon::Combine::Modulate2x: return "modulate_2x";
        case canon::Combine::Add: return "add";
        case canon::Combine::AlphaBlend: return "alpha_blend";
    }
    return "replace";
}

void writeTextureSource(json::Writer& w, const canon::TextureSource& source) {
    if (auto index = std::get_if<uint32_t>(&source)) {
        w.value(static_cast<int64_t>(*index));
        return;
    }
    w.beginObject();
    w.key("input");
    w.value(std::get<std::string>(source));
    w.endObject();
}

void writeComposite(json::Writer& w, const canon::CompositeTexture& composite) {
    w.beginObject();
    w.key("composite");
    w.beginObject();
    w.key("width");
    w.value(static_cast<int64_t>(composite.width));
    w.key("height");
    w.value(static_cast<int64_t>(composite.height));
    w.key("blend_mapping");
    w.value(composite.blendMappingVerified ? "verified" : "unverified");
    w.key("layers");
    w.beginArray();
    for (const canon::CompositeLayer& layer : composite.layers) {
        w.beginObject();
        w.key("texture");
        writeTextureSource(w, layer.texture);
        w.key("rect");
        w.beginArray();
        for (uint32_t v : layer.rect) w.value(static_cast<int64_t>(v));
        w.endArray();
        if (layer.blend) {
            w.key("blend");
            w.value(compositeBlendName(*layer.blend));
        }
        w.key("source");
        w.beginObject();
        w.key("blend_mode");
        w.value(static_cast<int64_t>(layer.sourceBlendMode));
        w.key("layer");
        w.value(static_cast<int64_t>(layer.sourceLayer));
        w.endObject();
        w.endObject();
    }
    w.endArray();
    w.endObject();
    w.endObject();
}

}  // namespace

void writeMaterialDocument(const canon::MaterialDocument& document, const std::filesystem::path& path,
                           const Provenance& provenance, const std::filesystem::path& bundleDir,
                           const std::string& uriPrefix, std::unordered_set<std::string>& writtenTextures) {
    std::ostringstream text;
    json::Writer w(text);
    w.beginObject();
    writeContractHeader(w, "material", document.subject, provenance);
    w.key("definition");
    w.beginObject();
    w.key("inputs");
    w.beginObject();
    for (const canon::ImageInput& input : document.imageInputs) {
        w.key(input.name);
        w.beginObject();
        w.key("kind");
        w.value("image");
        w.key("description");
        w.value(input.description);
        w.key("optional");
        w.value(true);
        w.endObject();
    }
    w.endObject();
    w.endObject();
    w.key("selection");
    w.beginObject();
    w.endObject();
    w.key("references");
    w.beginArray();
    w.endArray();

    w.key("layers");
    w.beginArray();
    for (const canon::SurfaceLayer& layer : document.layers) {
        w.beginObject();
        w.key("role");
        w.value(roleName(layer.role));
        w.key("texture");
        writeTextureSource(w, layer.texture);
        w.key("uv");
        if (auto set = std::get_if<canon::UvSetIndex>(&layer.uv)) {
            w.value("uv_set_" + std::to_string(set->index));
        } else {
            w.value("sphere_environment");
        }
        w.key("combine");
        w.value(combineName(layer.combine));
        w.endObject();
    }
    w.endArray();

    w.key("output");
    w.beginObject();
    if (document.framebufferBlend) {
        w.key("framebuffer_blend");
        w.value(framebufferBlendName(*document.framebufferBlend));
    }
    w.key("lit");
    w.value(document.lit);
    w.endObject();

    w.key("resources");
    w.beginObject();
    w.key("textures");
    w.beginArray();
    for (const canon::MaterialTexture& texture : document.textures) {
        if (auto composite = std::get_if<canon::CompositeTexture>(&texture)) {
            writeComposite(w, *composite);
            continue;
        }
        std::ostringstream entry;
        json::Writer ew(entry);
        writeTextureEntry(ew, std::get<canon::TextureRef>(texture), bundleDir, writtenTextures, uriPrefix);
        w.rawValue(entry.str());
    }
    w.endArray();
    w.endObject();
    w.endObject();

    std::filesystem::create_directories(path.parent_path());
    std::string bytes = text.str();
    writeFile(path, std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

}  // namespace husk::writers
