// Spec source: documentation/wowdev-wiki/wikitext/M2/.skin.wiki's
// M2GetPixelShaderID/M2GetVertexShaderID + s_modelShaderEffect table
// (fetched 2026-08-14). Test values below are worked by hand from that
// same page's transcribed formulas/table -- see TEST_DESIGN.md#Independent-
// transcription-convention -- not copied from src/m2_shader_names.cpp.

#include <doctest/doctest.h>

#include "../src/m2_shader_names.hpp"

using husk::m2::resolveShaderNames;

TEST_CASE("resolveShaderNames: opCount == 1, low bits clear -> Diffuse_T1 / Combiners_Opaque") {
    auto names = resolveShaderNames(0x0000, 1);
    CHECK(names.resolved);
    CHECK(names.pixel == "Combiners_Opaque");
    CHECK(names.vertex == "Diffuse_T1");
}

TEST_CASE("resolveShaderNames: opCount == 1, shaderId 0x10 (real corpus value) -> Diffuse_T1 / Combiners_Mod") {
    // 0x10 & 0x70 != 0 -> Combiners_Mod. 0x10 & 0x80 == 0, 0x10 & 0x4000 == 0 -> Diffuse_T1.
    auto names = resolveShaderNames(0x0010, 1);
    CHECK(names.resolved);
    CHECK(names.pixel == "Combiners_Mod");
    CHECK(names.vertex == "Diffuse_T1");
}

TEST_CASE("resolveShaderNames: opCount == 1, envmap bit set -> Diffuse_Env") {
    auto names = resolveShaderNames(0x0080, 1);
    CHECK(names.resolved);
    CHECK(names.vertex == "Diffuse_Env");
}

TEST_CASE("resolveShaderNames: opCount == 1, 0x4000 set (and no envmap) -> Diffuse_T2") {
    auto names = resolveShaderNames(0x4000, 1);
    CHECK(names.resolved);
    CHECK(names.vertex == "Diffuse_T2");
}

TEST_CASE("resolveShaderNames: opCount > 1, shaderId 0x4014 (real corpus value) -> Mod_Mod2x / T1_T2") {
    // lower = 0x4014 & 7 = 4; 0x4014 & 0x70 = 0x10 (nonzero) -> Mod branch, lower==4 -> Mod_Mod2x.
    // vertex: 0x4014 & 0x80 == 0, 0x4014 & 0x8 == 0, 0x4014 & 0x4000 != 0 -> Diffuse_T1_T2.
    auto names = resolveShaderNames(0x4014, 2);
    CHECK(names.resolved);
    CHECK(names.pixel == "Combiners_Mod_Mod2x");
    CHECK(names.vertex == "Diffuse_T1_T2");
}

TEST_CASE("resolveShaderNames: opCount > 1, low bits clear -> Opaque_Opaque / T1_T1") {
    auto names = resolveShaderNames(0x0000, 2);
    CHECK(names.resolved);
    CHECK(names.pixel == "Combiners_Opaque_Opaque");
    CHECK(names.vertex == "Diffuse_T1_T1");
}

TEST_CASE("resolveShaderNames: opCount > 1, envmap + second envmap bit -> Diffuse_Env_Env") {
    auto names = resolveShaderNames(0x0088, 2);
    CHECK(names.resolved);
    CHECK(names.vertex == "Diffuse_Env_Env");
}

TEST_CASE("resolveShaderNames: opCount > 1, envmap without second bit -> Diffuse_Env_T1") {
    auto names = resolveShaderNames(0x0080, 2);
    CHECK(names.resolved);
    CHECK(names.vertex == "Diffuse_Env_T1");
}

TEST_CASE("resolveShaderNames: 0x8000 table lookup, index 0 -> row 0 of the transcribed table") {
    auto names = resolveShaderNames(0x8000, 1);
    CHECK(names.resolved);
    CHECK(names.pixel == "Combiners_Opaque_Mod2xNA_Alpha");
    CHECK(names.vertex == "Diffuse_T1_Env");
}

TEST_CASE("resolveShaderNames: 0x8000 table lookup, index 29 (last real row) -> Illum / T1_T1") {
    auto names = resolveShaderNames(0x8000 | 29, 1);
    CHECK(names.resolved);
    CHECK(names.pixel == "Illum");
    CHECK(names.vertex == "Diffuse_T1_T1");
}

TEST_CASE("resolveShaderNames: 0x8000 table lookup, out-of-range index -> unresolved, not a guess") {
    auto names = resolveShaderNames(0x8000 | 30, 1);
    CHECK_FALSE(names.resolved);
}
