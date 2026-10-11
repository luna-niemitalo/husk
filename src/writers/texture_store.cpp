#include "writers/texture_store.hpp"

#include <variant>

namespace husk::writers {

std::string TextureStore::put(uint32_t fdid, const canon::TextureRef::Payload& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = written_.find(fdid);
    if (it != written_.end()) return it->second;
    std::string filename = std::to_string(fdid) + "." + textureFileExtension(payload.encoding);
    // Another process may be exporting into the same store: rename makes
    // the file appear whole or not at all.
    std::filesystem::path temp = dir_ / (filename + ".partial");
    writeFile(temp, payload.bytes);
    std::filesystem::rename(temp, dir_ / filename);
    return written_.emplace(fdid, std::move(filename)).first->second;
}

void externalizeTexture(canon::TextureRef& texture, TextureStore& store, const std::string& uriToStore, AssetUris& uris) {
    auto fdid = std::get_if<canon::FileDataId>(&texture.resolved.id);
    if (texture.state != canon::TextureRef::State::Resolved || !fdid) return;
    const canon::TextureRef::Payload* chosen =
        texture.rawPayload ? &*texture.rawPayload : (texture.payload ? &*texture.payload : nullptr);
    if (!chosen) return;
    uris[fdid->value] = uriToStore + store.put(fdid->value, *chosen);
    texture.payload.reset();
    texture.rawPayload.reset();
}

namespace {

void externalize(canon::InputValue& value, TextureStore& store, const std::string& uriToStore, AssetUris& uris) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, canon::TextureRef>) {
                externalizeTexture(v, store, uriToStore, uris);
            } else if constexpr (std::is_same_v<T, std::vector<canon::InputValue>>) {
                for (canon::InputValue& element : v) externalize(element, store, uriToStore, uris);
            } else if constexpr (std::is_same_v<T, std::vector<canon::NamedInput>>) {
                for (canon::NamedInput& field : v) externalize(field.value, store, uriToStore, uris);
            }
        },
        value.value);
}

}  // namespace

void externalizeTextures(canon::Model& model, TextureStore& store, const std::string& uriToStore, AssetUris& uris) {
    for (canon::Material& material : model.materials) {
        for (canon::MaterialLayer& layer : material.layers) externalizeTexture(layer.texture, store, uriToStore, uris);
    }
    for (canon::TextureSlot& slot : model.textureSlots) {
        for (canon::TextureRef& option : slot.options) externalizeTexture(option, store, uriToStore, uris);
    }
    for (canon::LiquidType& type : model.liquidTypes) {
        for (canon::LiquidTextureSlot& slot : type.textures) {
            for (canon::TextureRef& frame : slot.frames) externalizeTexture(frame, store, uriToStore, uris);
        }
    }
    for (canon::Model::OwnedSet& owned : model.placementSets) {
        for (canon::PlacedInstance& instance : owned.set.instances) {
            if (!instance.effect) continue;
            for (canon::NamedInput& input : instance.effect->inputs) externalize(input.value, store, uriToStore, uris);
        }
    }
}

}  // namespace husk::writers
