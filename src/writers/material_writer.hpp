#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>

#include "canon_material_document.hpp"
#include "writers/bundle_common.hpp"

// A `material` document (schema/material.schema.json, REFACTOR/MATERIALS.md).
namespace husk::writers {

// Writes `document` to `path`, a file under `bundleDir`; `uriPrefix` leads
// from it back to `bundleDir` ("../" from materials/). Texture payloads go
// under `bundleDir/textures/`.
void writeMaterialDocument(const canon::MaterialDocument& document, const std::filesystem::path& path,
                           const Provenance& provenance, const std::filesystem::path& bundleDir,
                           const std::string& uriPrefix, std::unordered_set<std::string>& writtenTextures);

}  // namespace husk::writers
