// src/zone_fog_extinction.hpp: fog_density_to_extinction on hand-computed
// values (scale 0.0005, floor 0.9, fill 1.5 + 5.5 * (1 - span / 500)).

#include <doctest/doctest.h>
#include <cmath>

#include "../src/zone_fog_extinction.hpp"

using husk::fog::densityToExtinction;

TEST_CASE("fog_density_to_extinction: a stored density scales by 0.0005 per yard") {
    auto e = densityToExtinction(4.5f, 2.0f, 0.0f, 0.0f, 0.5f);
    CHECK(e.main == doctest::Approx(0.00225));
    CHECK(e.height == doctest::Approx(0.001));
    // 12.1.5's median key: half fog 308 yards past the fog start.
    CHECK(std::log(2.0) / e.main == doctest::Approx(308.06).epsilon(0.001));
}

TEST_CASE("fog_density_to_extinction: the main density is floored at 0.9, the height density is not") {
    auto e = densityToExtinction(0.5f, 0.25f, 0.0f, 0.0f, 1.0f);
    CHECK(e.main == doctest::Approx(0.00045));
    CHECK(e.height == doctest::Approx(0.000125));
}

TEST_CASE("fog_density_to_extinction: a missing density is filled from the linear fog span") {
    // FogEnd 0 is raised to 10: span 10 -> (1 - 10/500) * 5.5 + 1.5 = 6.89.
    CHECK(densityToExtinction(0.0f, 0.0f, 0.0f, 0.0f, 1.0f).main == doctest::Approx(0.003445));
    // Span 400 - 400 * 0.5 = 200 -> (1 - 0.4) * 5.5 + 1.5 = 4.8.
    CHECK(densityToExtinction(0.0f, 0.0f, 400.0f, 0.5f, 1.0f).main == doctest::Approx(0.0024));
    // Spans past 500 or not positive -> 1.5; the scaler is clamped to [-1, 1].
    CHECK(densityToExtinction(0.0f, 0.0f, 1000.0f, 0.0f, 1.0f).main == doctest::Approx(0.00075));
    CHECK(densityToExtinction(0.0f, 0.0f, 100.0f, 5.0f, 1.0f).main == doctest::Approx(0.00075));
}

TEST_CASE("fog_density_to_extinction: no height scaler -> the height fog takes the unfloored main density") {
    CHECK(densityToExtinction(0.5f, 3.0f, 0.0f, 0.0f, 0.0f).height == doctest::Approx(0.00025));
    CHECK(densityToExtinction(0.0f, 3.0f, 0.0f, 0.0f, 0.0f).height == doctest::Approx(0.003445));
    // With a height scaler, a stored 0 stays 0: no extinction inside the height fog.
    CHECK(densityToExtinction(4.5f, 0.0f, 0.0f, 0.0f, 0.5f).height == 0.0f);
}
