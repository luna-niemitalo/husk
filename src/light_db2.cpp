#include "light_db2.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <filesystem>

#include "db2table.hpp"

namespace husk::lightdb {

namespace {

std::string tablePath(const std::string& db2Dir, const char* file) {
    return (std::filesystem::path(db2Dir) / file).string();
}

float asFloat(const std::optional<uint32_t>& v) { return std::bit_cast<float>(v.value_or(0)); }

template <size_t N>
std::array<float, N> floats(const std::optional<std::vector<uint32_t>>& v) {
    std::array<float, N> out{};
    if (!v) return out;
    for (size_t i = 0; i < N && i < v->size(); ++i) out[i] = std::bit_cast<float>((*v)[i]);
    return out;
}

const std::vector<std::string> kFogScalars = {
    "LightParamID",    "Time",           "FogEnd",       "FogScaler",          "FogDensity",
    "FogHeight",       "FogHeightScaler", "FogHeightDensity", "FogZScalar",    "MainFogStartDist",
    "MainFogEndDist",  "SunFogAngle",    "SunFogStrength", "EndFogColorDistance", "FogStartOffset",
    "SkyFogColor",     "EndFogColor",    "SunFogColor",  "FogHeightColor",     "EndFogHeightColor"};

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string lightPath = tablePath(db2Dir, "light.db2");
    std::string dataPath = tablePath(db2Dir, "lightdata.db2");
    std::string mapPath = tablePath(db2Dir, "map.db2");
    auto lights = db2table::readNamedColumns(lightPath, dbdDir, {"ID", "GameFalloffStart", "GameFalloffEnd", "ContinentID"}, err);
    auto lightArrays = db2table::readNamedArrayColumns(lightPath, dbdDir, {"GameCoords", "LightParamsID"}, err);
    auto keys = db2table::readNamedColumns(dataPath, dbdDir, kFogScalars, err);
    auto keyArrays = db2table::readNamedArrayColumns(
        dataPath, dbdDir, {"FogHeightCoefficients", "MainFogCoefficients", "HeightDensityFogCoeff"}, err);
    auto mapIds = db2table::readNamedColumns(mapPath, dbdDir, {"ID"}, err);
    auto mapDirs = db2table::readNamedStringColumns(mapPath, dbdDir, {"Directory"}, err);
    if (!lights || !lightArrays || !keys || !keyArrays || !mapIds || !mapDirs) return std::nullopt;
    if (lights->size() != lightArrays->size() || keys->size() != keyArrays->size() || mapIds->size() != mapDirs->size()) {
        err << "light db2: column reads disagree on row count\n";
        return std::nullopt;
    }

    Data out;
    for (size_t r = 0; r < lights->size(); ++r) {
        const auto& row = (*lights)[r];
        Zone zone;
        zone.id = row[0].value_or(0);
        zone.falloffStart = asFloat(row[1]);
        zone.falloffEnd = asFloat(row[2]);
        zone.map = static_cast<int16_t>(row[3].value_or(0));
        zone.position = floats<3>((*lightArrays)[r][0]);
        if (const auto& params = (*lightArrays)[r][1]) {
            for (size_t i = 0; i < zone.params.size() && i < params->size(); ++i) zone.params[i] = static_cast<uint16_t>((*params)[i]);
        }
        out.zones.push_back(zone);
    }
    for (size_t r = 0; r < keys->size(); ++r) {
        const auto& row = (*keys)[r];
        FogKey key;
        key.time = row[1].value_or(0);
        float* scalars[] = {&key.fogEnd, &key.fogScaler, &key.fogDensity, &key.fogHeight, &key.fogHeightScaler,
                            &key.fogHeightDensity, &key.fogZScalar, &key.mainFogStartDist, &key.mainFogEndDist,
                            &key.sunFogAngle, &key.sunFogStrength, &key.endFogColorDistance, &key.fogStartOffset};
        for (size_t i = 0; i < std::size(scalars); ++i) *scalars[i] = asFloat(row[2 + i]);
        uint32_t* colors[] = {&key.skyFogColor, &key.endFogColor, &key.sunFogColor, &key.fogHeightColor, &key.endFogHeightColor};
        for (size_t i = 0; i < std::size(colors); ++i) *colors[i] = row[15 + i].value_or(0);
        key.fogHeightCoefficients = floats<4>((*keyArrays)[r][0]);
        key.mainFogCoefficients = floats<4>((*keyArrays)[r][1]);
        key.heightDensityFogCoefficients = floats<4>((*keyArrays)[r][2]);
        out.keysByParams[row[0].value_or(0)].push_back(key);
    }
    for (auto& [params, list] : out.keysByParams) {
        std::sort(list.begin(), list.end(), [](const FogKey& a, const FogKey& b) { return a.time < b.time; });
    }
    for (size_t r = 0; r < mapIds->size(); ++r) {
        std::string dir = (*mapDirs)[r][0].value_or("");
        std::transform(dir.begin(), dir.end(), dir.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!dir.empty()) out.mapIds[dir] = static_cast<int16_t>((*mapIds)[r][0].value_or(0));
    }
    return out;
}

}  // namespace husk::lightdb
