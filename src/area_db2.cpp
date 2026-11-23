#include "area_db2.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

#include "db2table.hpp"

namespace husk::areadb {

namespace {

using BitNames = std::vector<std::pair<uint32_t, const char*>>;

// wowdev.wiki DB/AreaTable, "Version 10.2" tables, names as written there.
const BitNames kFlags0 = {
    {0x00000001, "EmitBreathParticles"},
    {0x00000002, "BreathParticlesOverrideParent"},
    {0x00000004, "OnMapDungeon"},
    {0x00000008, "AllowTradeChannel"},
    {0x00000010, "EnemiesPvPFlagged"},
    {0x00000020, "AllowResting"},
    {0x00000040, "AllowDueling"},
    {0x00000080, "FreeForAllPvP"},
    {0x00000100, "LinkedChat"},
    {0x00000200, "LinkedChatSpecialArea"},
    {0x00000400, "ForceThisAreaWhenOnDynamicTransport"},
    {0x00000800, "NoPvP"},
    {0x00001000, "NoGhostOnRelease"},
    {0x00002000, "SubZoneAmbientMultiplier"},
    {0x00004000, "EnableFlightBoundsOnMap"},
    {0x00008000, "PVPPOI"},
    {0x00010000, "NoChatChannels"},
    {0x00020000, "AreaNotInUse"},
    {0x00040000, "Contested"},
    {0x00080000, "NoPlayerSummoning"},
    {0x00100000, "NoDuelingIfTournamentRealm"},
    {0x00200000, "PlayersCallGuards"},
    {0x00400000, "HordeResting"},
    {0x00800000, "AllianceResting"},
    {0x01000000, "CombatZone"},
    {0x02000000, "ForceIndoors"},
    {0x04000000, "ForceOutdoors"},
    {0x08000000, "AllowHearthAndRessurectFromArea"},
    {0x10000000, "NoLocalDefenseChannel"},
    {0x20000000, "OnlyEvaluateGhostBindOnce"},
    {0x40000000, "IsSubzone"},
    {0x80000000, "DontEvaluateGraveyardFromClient"},
};

const BitNames kFlags1 = {
    {0x00000001, "ForceMicroDungeonArtMap"},
    {0x00000002, "UseSubzonePlayerLoot"},
    {0x00000004, "AllowPetBattleDuelingEvenIfNoDuelingAllowed"},
    {0x00000008, "UseMapTransferLocsForCemeteries"},
    {0x00000010, "IsGarrison"},
    {0x00000020, "UseSubzoneForChatChannel"},
    {0x00000040, "DontRealmCoalesceChatChannel"},
    {0x00000080, "NotExplorable"},
    {0x00000100, "DontUseParentMapForCemeteries"},
    {0x00000200, "DontShowSanctuaryText"},
    {0x00000400, "CrossFactionZoneChat"},
    {0x00000800, "ForceNoResting"},
    {0x00001000, "AllowWarModeToggle"},
};

const BitNames kMountFlags = {
    {0x01, "AllowGroundMounts"},
    {0x02, "AllowFlyingMounts"},
    {0x04, "AllowSurfaceSwimmingMounts"},
    {0x08, "AllowUnderwaterSwimmingMounts"},
    {0x10, "ClientEnforcesMount"},
};

void appendSet(std::vector<std::string>& out, uint32_t word, const BitNames& names) {
    for (const auto& [bit, name] : names) {
        if (word & bit) out.emplace_back(name);
    }
}

}  // namespace

std::vector<std::string> flagNames(const std::array<uint32_t, 2>& flags, uint32_t mountFlags) {
    std::vector<std::string> out;
    appendSet(out, flags[0], kFlags0);
    appendSet(out, flags[1], kFlags1);
    appendSet(out, mountFlags, kMountFlags);
    return out;
}

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string path = (std::filesystem::path(db2Dir) / "areatable.db2").string();
    auto rows = db2table::readNamedColumns(path, dbdDir, {"ID", "ParentAreaID", "MountFlags"}, err);
    auto names = db2table::readNamedStringColumns(path, dbdDir, {"AreaName_lang"}, err);
    auto flags = db2table::readNamedArrayColumns(path, dbdDir, {"Flags"}, err);
    if (!rows || !names || !flags) return std::nullopt;
    if (rows->size() != names->size() || rows->size() != flags->size()) {
        err << "areatable.db2: column reads disagree on row count (" << rows->size() << ", " << names->size() << ", "
            << flags->size() << ")\n";
        return std::nullopt;
    }
    Data out;
    for (size_t r = 0; r < rows->size(); ++r) {
        const auto& id = (*rows)[r][0];
        if (!id) throw std::runtime_error("AreaTable.ID: expected a value in row " + std::to_string(r) + ", got none");
        Row row{(*names)[r][0].value_or(""), (*rows)[r][1].value_or(0), std::nullopt, (*rows)[r][2]};
        if (const auto& words = (*flags)[r][0]) {
            // The current layouts declare Flags<32>[2]; anything else is a layout husk hasn't seen.
            if (words->size() != 2) {
                throw std::runtime_error("AreaTable.Flags of row " + std::to_string(*id) + ": expected 2 words, got " +
                                         std::to_string(words->size()));
            }
            row.flags = std::array<uint32_t, 2>{(*words)[0], (*words)[1]};
        }
        out[*id] = std::move(row);
    }
    return out;
}

}  // namespace husk::areadb
