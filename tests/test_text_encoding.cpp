#include <doctest/doctest.h>

#include <sstream>
#include <stdexcept>
#include <string>

#include "json_writer.hpp"
#include "text_encoding.hpp"

using husk::text::fromGameText;
using husk::text::isUtf8;

TEST_CASE("text::fromGameText: UTF-8 passes through, Windows-1252 is decoded") {
    CHECK(fromGameText("Nefarian's Lair") == "Nefarian's Lair");
    CHECK(fromGameText("Caf\xC3\xA9") == "Caf\xC3\xA9");                     // already UTF-8
    CHECK(fromGameText("Nefarian\x92s Lair") == "Nefarian\xE2\x80\x99s Lair");  // 0x92: right quote
    CHECK(fromGameText("Caf\xE9") == "Caf\xC3\xA9");                          // Latin-1 range
    CHECK(fromGameText("\x80") == "\xE2\x82\xAC");                            // euro sign
    CHECK(fromGameText("\x81") == "\xC2\x81");                                // undefined: same code point
}

TEST_CASE("text::isUtf8: rejects stray continuations, overlongs and truncated sequences") {
    CHECK(isUtf8(""));
    CHECK(isUtf8("plain"));
    CHECK(isUtf8("\xE2\x80\x99"));
    CHECK(isUtf8("\xF0\x9F\x98\x80"));
    CHECK_FALSE(isUtf8("\x92"));
    CHECK_FALSE(isUtf8("\xC0\x80"));  // overlong
    CHECK_FALSE(isUtf8("\xE2\x80"));  // truncated
    CHECK_FALSE(isUtf8("\xE2\x28\xA1"));
}

TEST_CASE("json::Writer refuses text that isn't UTF-8") {
    std::ostringstream out;
    husk::json::Writer w(out);
    w.beginObject();
    w.key("name");
    CHECK_THROWS_AS(w.value(std::string("Nefarian\x92s")), std::logic_error);
}
