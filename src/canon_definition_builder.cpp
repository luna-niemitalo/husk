#include "canon_definition_builder.hpp"

#include <algorithm>

namespace husk::canon {

namespace {

// name.empty() covers both real gaps namedChoicesForModel can hand back
// (a swatch-only Name_lang == "0" choice, or an unresolved/dangling
// categoryId == 0) -- neither is fabricated a name here, per Ref's own
// "name empty, source None" convention for "no real fact to attach" (see
// canon_ref.hpp's NameSource doc comment).
Ref makeRef(const std::string& table, uint32_t row, const std::string& name) {
    Ref ref;
    ref.id = Db2Row{table, row};
    if (!name.empty()) {
        ref.name = name;
        ref.source = NameSource::Db2;
    }
    return ref;
}

uint32_t dbRow(const Ref& ref) { return std::get<Db2Row>(ref.id).row; }

}  // namespace

Definition assembleDefinition(const chrcustomization::Data& data, uint32_t chrModelId, std::ostream& err) {
    Definition result;
    result.chrModelId = chrModelId;

    std::vector<chrcustomization::NamedChoice> named = chrcustomization::namedChoicesForModel(data, chrModelId, err);
    if (named.empty()) return result;

    std::vector<Option> options;
    for (const auto& nc : named) {
        auto optIt = std::find_if(options.begin(), options.end(),
                                   [&](const Option& o) { return dbRow(o.ref) == nc.optionId; });
        if (optIt == options.end()) {
            Option opt;
            opt.ref = makeRef("ChrCustomizationOption", nc.optionId, nc.optionName);
            opt.orderIndex = nc.optionOrderIndex;
            opt.category.ref = makeRef("ChrCustomizationCategory", nc.categoryId, nc.categoryName);
            opt.category.orderIndex = nc.categoryOrderIndex;
            options.push_back(std::move(opt));
            optIt = options.end() - 1;
        }

        Choice choice;
        choice.ref = makeRef("ChrCustomizationChoice", nc.choiceId, nc.choiceName);
        choice.orderIndex = nc.choiceOrderIndex;
        if (nc.resolution.geosetId.has_value()) choice.geoset = Geoset::fromRawId(*nc.resolution.geosetId);
        optIt->choices.push_back(std::move(choice));
    }

    // Deterministic real-UI-order sort -- see this file's own header doc
    // comment for why this is an intentional divergence from
    // export_extras.cpp's own insertion-order-preserving loop.
    std::sort(options.begin(), options.end(), [](const Option& a, const Option& b) {
        if (a.orderIndex != b.orderIndex) return a.orderIndex < b.orderIndex;
        return dbRow(a.ref) < dbRow(b.ref);
    });
    for (auto& opt : options) {
        std::sort(opt.choices.begin(), opt.choices.end(), [](const Choice& a, const Choice& b) {
            if (a.orderIndex != b.orderIndex) return a.orderIndex < b.orderIndex;
            return dbRow(a.ref) < dbRow(b.ref);
        });
    }

    result.options = std::move(options);
    return result;
}

}  // namespace husk::canon
