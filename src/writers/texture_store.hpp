#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

#include "canon_model.hpp"
#include "writers/bundle_common.hpp"  // AssetUris

// An export's shared texture directory (export-world's <out>/textures/):
// each FileDataID's payload is written there once, and documents refer to
// it by uri instead of carrying their own copy (REFACTOR/BUNDLE_FORMAT.md
// "Textures"). Safe to share between threads.
namespace husk::writers {

class TextureStore {
public:
    explicit TextureStore(std::filesystem::path dir) : dir_(std::move(dir)) {}

    // The payload's filename in the store (`<fdid>.<ext>`), written on the
    // first call for that FileDataID.
    std::string put(uint32_t fdid, const canon::TextureRef::Payload& payload);

private:
    std::filesystem::path dir_;
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::string> written_;
};

// Where one bundle's textures go instead of its own textures/: the store,
// and the store's directory relative to the bundle ("../../textures/").
struct StoreLink {
    TextureStore* store = nullptr;
    std::string uriToStore;
};

// Moves every resolved, FileDataID-named texture payload `model` carries
// (material layers, texture slot options, liquid frames, effect image
// inputs) into `store`, leaving the reference without a payload, and
// records its uri (`uriToStore` + filename, relative to the model's
// bundle) in `uris` for the writer.
void externalizeTextures(canon::Model& model, TextureStore& store, const std::string& uriToStore, AssetUris& uris);

// The same for one texture.
void externalizeTexture(canon::TextureRef& texture, TextureStore& store, const std::string& uriToStore, AssetUris& uris);

}  // namespace husk::writers
