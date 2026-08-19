#include "appearance_string.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace husk::appearance {

namespace {

bool isSlotChar(char c) { return (c >= 'A' && c <= 'Z') || c == '_'; }

int parseInt(const std::string& field, const std::string& token) {
    if (token.empty() || !std::all_of(token.begin(), token.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        throw ParseError("husk-appearance: " + field + ": expected a non-negative integer, got '" + token + "'");
    }
    return std::stoi(token);
}

std::vector<std::string> splitNonEmpty(const std::string& text, char sep) {
    std::vector<std::string> parts;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, sep)) {
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

GearEntry parseGearEntry(const std::string& token) {
    size_t colon = token.find(':');
    if (colon == std::string::npos) {
        throw ParseError("husk-appearance: gear: expected 'SLOT:id', got '" + token + "'");
    }
    std::string slot = token.substr(0, colon);
    if (slot.empty() || !std::all_of(slot.begin(), slot.end(), isSlotChar)) {
        throw ParseError("husk-appearance: gear: expected slot matching [A-Z_]+, got '" + slot + "'");
    }
    GearEntry entry;
    entry.slot = slot;
    entry.itemModifiedAppearanceId = parseInt("gear", token.substr(colon + 1));
    return entry;
}

}  // namespace

AppearanceString parse(const std::string& text) {
    std::stringstream ss(text);
    std::string tag;
    ss >> tag;
    if (tag != "husk-appearance/1") {
        throw ParseError("husk-appearance: expected version tag 'husk-appearance/1', got '" + tag + "'");
    }

    AppearanceString result;
    bool sawRace = false, sawSex = false;
    std::string field;
    while (ss >> field) {
        size_t eq = field.find('=');
        if (eq == std::string::npos) {
            throw ParseError("husk-appearance: expected 'key=value', got '" + field + "'");
        }
        std::string key = field.substr(0, eq);
        std::string value = field.substr(eq + 1);

        if (key == "race") {
            result.raceId = parseInt("race", value);
            sawRace = true;
        } else if (key == "sex") {
            result.sexId = parseInt("sex", value);
            if (result.sexId != 0 && result.sexId != 1) {
                throw ParseError("husk-appearance: sex: expected 0 or 1, got '" + value + "'");
            }
            sawSex = true;
        } else if (key == "cust") {
            for (const auto& token : splitNonEmpty(value, ',')) {
                result.customizationChoiceIds.push_back(parseInt("cust", token));
            }
        } else if (key == "gear") {
            for (const auto& token : splitNonEmpty(value, ',')) {
                result.gear.push_back(parseGearEntry(token));
            }
        } else {
            throw ParseError("husk-appearance: unknown field '" + key + "'");
        }
    }
    (void)sawRace;
    (void)sawSex;
    return result;
}

std::string serialize(const AppearanceString& value) {
    std::vector<int> cust = value.customizationChoiceIds;
    std::sort(cust.begin(), cust.end());

    std::vector<GearEntry> gear = value.gear;
    std::sort(gear.begin(), gear.end(), [](const GearEntry& a, const GearEntry& b) {
        if (a.slot != b.slot) return a.slot < b.slot;
        return a.itemModifiedAppearanceId < b.itemModifiedAppearanceId;
    });

    std::ostringstream out;
    out << "husk-appearance/1";
    out << " race=" << value.raceId;
    out << " sex=" << value.sexId;

    out << " cust=";
    for (size_t i = 0; i < cust.size(); ++i) {
        if (i) out << ',';
        out << cust[i];
    }

    out << " gear=";
    for (size_t i = 0; i < gear.size(); ++i) {
        if (i) out << ',';
        out << gear[i].slot << ':' << gear[i].itemModifiedAppearanceId;
    }

    return out.str();
}

}  // namespace husk::appearance
