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

// LightData's scalar columns, read as one row: LightParamID, then the
// integers and the floats in the order keyOf stores them.
const std::vector<std::string> kKeyIntColumns = {
    "LightParamID",        "Time",
    "DirectColor",         "AmbientColor",           "SkyTopColor",           "SkyMiddleColor",
    "SkyBand1Color",       "SkyBand2Color",          "SkySmogColor",          "SkyFogColor",
    "SunColor",            "CloudSunColor",          "CloudEmissiveColor",    "CloudLayer1AmbientColor",
    "CloudLayer2AmbientColor", "OceanCloseColor",    "OceanFarColor",         "RiverCloseColor",
    "RiverFarColor",       "ShadowOpacity",          "HorizonAmbientColor",   "GroundAmbientColor",
    "EndFogColor",         "SunFogColor",            "FogHeightColor",        "EndFogHeightColor",
    "ColorGradingFileDataID", "DarkerColorGradingFileDataID", "Field_10_0_0_44649_042"};
const std::vector<std::string> kKeyFloatColumns = {
    "FogEnd",          "FogScaler",      "FogDensity",       "FogHeight",          "FogHeightScaler",
    "FogHeightDensity", "FogZScalar",    "MainFogStartDist", "MainFogEndDist",     "SunFogAngle",
    "CloudDensity",    "EndFogColorDistance", "FogStartOffset", "SunFogStrength",  "Field_12_0_0_63854_043"};

std::vector<std::string> concat(std::vector<std::string> a, const std::vector<std::string>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Key keyOf(const std::vector<std::optional<uint32_t>>& row, const std::vector<std::optional<std::vector<uint32_t>>>& arrays) {
    Key key;
    uint32_t* ints[] = {&key.time,
                        &key.directColor,         &key.ambientColor,           &key.skyTopColor,
                        &key.skyMiddleColor,      &key.skyBand1Color,          &key.skyBand2Color,
                        &key.skySmogColor,        &key.skyFogColor,            &key.sunColor,
                        &key.cloudSunColor,       &key.cloudEmissiveColor,     &key.cloudLayer1AmbientColor,
                        &key.cloudLayer2AmbientColor, &key.oceanCloseColor,    &key.oceanFarColor,
                        &key.riverCloseColor,     &key.riverFarColor,          &key.shadowOpacity,
                        &key.horizonAmbientColor, &key.groundAmbientColor,     &key.endFogColor,
                        &key.sunFogColor,         &key.fogHeightColor,         &key.endFogHeightColor,
                        &key.colorGradingFileDataId, &key.darkerColorGradingFileDataId, &key.field_10_0_0_44649_042};
    static_assert(std::size(ints) == 28);
    for (size_t i = 0; i < std::size(ints); ++i) *ints[i] = row[1 + i].value_or(0);
    float* reals[] = {&key.fogEnd,           &key.fogScaler,   &key.fogDensity,       &key.fogHeight,
                      &key.fogHeightScaler,  &key.fogHeightDensity, &key.fogZScalar,  &key.mainFogStartDist,
                      &key.mainFogEndDist,   &key.sunFogAngle, &key.cloudDensity,     &key.endFogColorDistance,
                      &key.fogStartOffset,   &key.sunFogStrength, &key.field_12_0_0_63854_043};
    static_assert(std::size(reals) == 15);
    size_t floatsAt = kKeyIntColumns.size();
    for (size_t i = 0; i < std::size(reals); ++i) *reals[i] = asFloat(row[floatsAt + i]);
    key.fogHeightCoefficients = floats<4>(arrays[0]);
    key.mainFogCoefficients = floats<4>(arrays[1]);
    key.heightDensityFogCoefficients = floats<4>(arrays[2]);
    return key;
}

const std::vector<std::string> kParamsColumns = {
    "ID",          "HighlightSky",       "LightSkyboxID",     "CloudTypeID",       "Glow",
    "WaterShallowAlpha", "WaterDeepAlpha", "OceanShallowAlpha", "OceanDeepAlpha",  "Flags",
    "SsaoSettingsID", "SunPolar",        "SunAzimuth",        "SunAttenuationStart", "SunAttenuationEnd",
    "Field_12_0_0_63534_016", "Field_12_0_0_63534_017", "Field_12_0_0_63534_018", "Field_12_0_0_63534_019",
    "Field_12_0_0_63534_020", "Field_12_0_0_63534_021", "Field_12_0_0_63534_022", "Field_12_0_0_63534_023",
    "Field_12_0_0_63534_024", "Field_12_0_0_63534_025", "Field_12_0_0_63534_026", "Field_12_0_0_63534_027",
    "Field_12_0_0_63534_028", "Field_12_0_0_63534_029", "Field_12_0_1_65617_030", "Field_12_0_1_65617_031"};

Params paramsOf(const std::vector<std::optional<uint32_t>>& row, const std::vector<std::optional<std::vector<uint32_t>>>& arrays) {
    Params p;
    p.id = row[0].value_or(0);
    p.highlightSky = row[1].value_or(0) != 0;
    p.lightSkyboxId = static_cast<uint16_t>(row[2].value_or(0));
    p.cloudTypeId = static_cast<uint8_t>(row[3].value_or(0));
    p.glow = asFloat(row[4]);
    p.waterShallowAlpha = asFloat(row[5]);
    p.waterDeepAlpha = asFloat(row[6]);
    p.oceanShallowAlpha = asFloat(row[7]);
    p.oceanDeepAlpha = asFloat(row[8]);
    p.flags = row[9].value_or(0);
    p.ssaoSettingsId = row[10].value_or(0);
    p.sunPolar = asFloat(row[11]);
    p.sunAzimuth = asFloat(row[12]);
    p.sunAttenuationStart = asFloat(row[13]);
    p.sunAttenuationEnd = asFloat(row[14]);
    for (size_t i = 0; i < p.unnamed.size(); ++i) p.unnamed[i] = row[15 + i].value_or(0);
    p.overrideCelestialSphere = floats<3>(arrays[0]);
    p.overrideSunPosition = floats<3>(arrays[1]);
    return p;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string lightPath = tablePath(db2Dir, "light.db2");
    std::string dataPath = tablePath(db2Dir, "lightdata.db2");
    std::string paramsPath = tablePath(db2Dir, "lightparams.db2");
    std::string skyboxPath = tablePath(db2Dir, "lightskybox.db2");
    std::string mapPath = tablePath(db2Dir, "map.db2");
    auto lights = db2table::readNamedColumns(lightPath, dbdDir, {"ID", "GameFalloffStart", "GameFalloffEnd", "ContinentID"}, err);
    auto lightArrays = db2table::readNamedArrayColumns(lightPath, dbdDir, {"GameCoords", "LightParamsID"}, err);
    auto keys = db2table::readNamedColumns(dataPath, dbdDir, concat(kKeyIntColumns, kKeyFloatColumns), err);
    auto keyArrays = db2table::readNamedArrayColumns(
        dataPath, dbdDir, {"FogHeightCoefficients", "MainFogCoefficients", "HeightDensityFogCoeff"}, err);
    auto params = db2table::readNamedColumns(paramsPath, dbdDir, kParamsColumns, err);
    auto paramArrays = db2table::readNamedArrayColumns(paramsPath, dbdDir, {"OverrideCelestialSphere", "OverrideSunPosition"}, err);
    auto skyboxes = db2table::readNamedColumns(skyboxPath, dbdDir, {"ID", "Flags", "SkyboxFileDataID", "CelestialSkyboxFileDataID"}, err);
    auto skyboxNames = db2table::readNamedStringColumns(skyboxPath, dbdDir, {"Name"}, err);
    auto mapIds = db2table::readNamedColumns(mapPath, dbdDir, {"ID"}, err);
    auto mapDirs = db2table::readNamedStringColumns(mapPath, dbdDir, {"Directory"}, err);
    if (!lights || !lightArrays || !keys || !keyArrays || !params || !paramArrays || !skyboxes || !skyboxNames || !mapIds ||
        !mapDirs)
        return std::nullopt;
    if (lights->size() != lightArrays->size() || keys->size() != keyArrays->size() || params->size() != paramArrays->size() ||
        skyboxes->size() != skyboxNames->size() || mapIds->size() != mapDirs->size()) {
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
        if (const auto& slots = (*lightArrays)[r][1]) {
            for (size_t i = 0; i < zone.params.size() && i < slots->size(); ++i) zone.params[i] = static_cast<uint16_t>((*slots)[i]);
        }
        out.zones.push_back(zone);
    }
    for (size_t r = 0; r < keys->size(); ++r) {
        out.keysByParams[(*keys)[r][0].value_or(0)].push_back(keyOf((*keys)[r], (*keyArrays)[r]));
    }
    for (auto& [id, list] : out.keysByParams) {
        std::sort(list.begin(), list.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
    }
    for (size_t r = 0; r < params->size(); ++r) {
        Params p = paramsOf((*params)[r], (*paramArrays)[r]);
        out.params[p.id] = p;
    }
    for (size_t r = 0; r < skyboxes->size(); ++r) {
        const auto& row = (*skyboxes)[r];
        Skybox s{row[0].value_or(0), (*skyboxNames)[r][0].value_or(""), row[1].value_or(0), row[2].value_or(0),
                 row[3].value_or(0)};
        out.skyboxes[s.id] = s;
    }
    for (size_t r = 0; r < mapIds->size(); ++r) {
        std::string dir = (*mapDirs)[r][0].value_or("");
        std::transform(dir.begin(), dir.end(), dir.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!dir.empty()) out.mapIds[dir] = static_cast<int16_t>((*mapIds)[r][0].value_or(0));
    }
    return out;
}

}  // namespace husk::lightdb
