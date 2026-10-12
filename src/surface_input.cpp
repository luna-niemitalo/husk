#include "surface_input.hpp"

namespace husk::surfaceinput {

namespace {

std::optional<canon::Ref> sprayRef(uint32_t row) {
    if (row == 0) return std::nullopt;
    return canon::Ref{canon::Db2Row{"SpellVisualEffectName", row}, "", canon::NameSource::None};
}

}  // namespace

uint32_t SurfaceTable::add(uint32_t terrainType) {
    auto [it, added] = index_.emplace(terrainType, static_cast<uint32_t>(entries_.size()));
    if (!added) return it->second;
    canon::SurfaceType s;
    s.ref = canon::Ref{canon::Db2Row{"TerrainType", terrainType}, "", canon::NameSource::None};
    const surfacedb::TerrainType* row = nullptr;
    if (types_) {
        auto found = types_->types.find(terrainType);
        if (found != types_->types.end()) row = &found->second;
    }
    if (row) {
        if (!row->name.empty()) {
            s.ref.name = row->name;
            s.ref.source = canon::NameSource::Db2;
        }
        s.soundClass = canon::Ref{canon::Db2Row{"TerrainTypeSounds", row->soundId}, row->soundName,
                                  row->soundName.empty() ? canon::NameSource::None : canon::NameSource::Db2};
        s.footstepSprayRun = sprayRef(row->footstepSprayRun);
        s.footstepSprayWalk = sprayRef(row->footstepSprayWalk);
        s.flags = row->flags;
    }
    entries_.push_back(std::move(s));
    return it->second;
}

}  // namespace husk::surfaceinput
