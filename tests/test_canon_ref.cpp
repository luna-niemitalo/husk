// Tests for husk::canon::Ref/Identity/NameSource (src/canon_ref.hpp) -- a
// pure value type, so these cover construction/discrimination, not logic.

#include <doctest/doctest.h>

#include "canon_ref.hpp"

using namespace husk::canon;

TEST_CASE("Identity: each alternative constructs and discriminates") {
    Identity fdid = FileDataId{12345};
    CHECK(std::holds_alternative<FileDataId>(fdid));
    CHECK(std::get<FileDataId>(fdid).value == 12345);

    Identity row = Db2Row{"chrmodel", 20};
    CHECK(std::holds_alternative<Db2Row>(row));
    CHECK(std::get<Db2Row>(row).table == "chrmodel");
    CHECK(std::get<Db2Row>(row).row == 20);

    Identity idx = RecordIndex{7};
    CHECK(std::holds_alternative<RecordIndex>(idx));
    CHECK(std::get<RecordIndex>(idx).value == 7);

    Identity none = None{};
    CHECK(std::holds_alternative<None>(none));
}

TEST_CASE("Identity: default-constructs to None") {
    Identity id;
    CHECK(std::holds_alternative<None>(id));
}

TEST_CASE("Ref: carries each identity kind alongside a name and matching source") {
    Ref texture{FileDataId{148134}, "scalpupperhair00_08", NameSource::Listfile};
    CHECK(std::get<FileDataId>(texture.id).value == 148134);
    CHECK(texture.name == "scalpupperhair00_08");
    CHECK(texture.source == NameSource::Listfile);

    Ref choice{Db2Row{"chrcustomizationchoice", 42}, "Blonde 01", NameSource::Db2};
    CHECK(std::get<Db2Row>(choice.id).row == 42);
    CHECK(choice.source == NameSource::Db2);

    Ref bone{RecordIndex{7}, "bone_7", NameSource::Synthesized};
    CHECK(std::get<RecordIndex>(bone.id).value == 7);
    CHECK(bone.source == NameSource::Synthesized);
}

TEST_CASE("Ref: default state is a nameless None identity with NameSource::None") {
    Ref ref;
    CHECK(std::holds_alternative<None>(ref.id));
    CHECK(ref.name.empty());
    CHECK(ref.source == NameSource::None);
}
