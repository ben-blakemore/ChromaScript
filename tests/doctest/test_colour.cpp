#include "doctest/doctest.h"

#include <cmath>
#include <string>
#include "dsl/utils.hpp"

namespace {

struct NamedCase { const char *name; std::array<float, 3> rgb; };
const NamedCase named_colours[] = {
    {"red", {1.0f, 0.0f, 0.0f}},
    {"red-orange", {1.0f, 0.25f, 0.0f}},
    {"orange", {1.0f, 0.5f, 0.0f}},
    {"orange-yellow", {1.0f, 0.75f, 0.0f}},
    {"yellow", {1.0f, 1.0f, 0.0f}},
    {"yellow-green", {0.5f, 1.0f, 0.0f}},
    {"green", {0.0f, 1.0f, 0.0f}},
    {"green-blue", {0.0f, 1.0f, 1.0f}},
    {"blue", {0.0f, 0.0f, 1.0f}},
    {"blue-violet", {0.25f, 0.0f, 1.0f}},
    {"violet", {0.5f, 0.0f, 1.0f}},
    {"violet-red", {1.0f, 0.0f, 0.5f}},
    {"cyan", {0.0f, 1.0f, 1.0f}},
    {"magenta", {1.0f, 0.0f, 1.0f}},
    {"white", {1.0f, 1.0f, 1.0f}},
    {"grey", {0.5f, 0.5f, 0.5f}},
    {"black", {0.0f, 0.0f, 0.0f}},
};

} // namespace

TEST_CASE("HSV conversion produces the expected RGB values")
{
    struct Case { const char *name; float hue; std::array<float, 3> rgb; };
    const Case conversion_cases[] = {
        {"red", 0.0f, {1.0f, 0.0f, 0.0f}},
        {"yellow", 1.0f / 6.0f, {1.0f, 1.0f, 0.0f}},
        {"green", 1.0f / 3.0f, {0.0f, 1.0f, 0.0f}},
        {"cyan", 0.5f, {0.0f, 1.0f, 1.0f}},
        {"blue", 2.0f / 3.0f, {0.0f, 0.0f, 1.0f}},
        {"magenta", 5.0f / 6.0f, {1.0f, 0.0f, 1.0f}},
    };
    for (const Case &colour_case : conversion_cases) {
        SUBCASE((std::string("HSV conversion: ") + colour_case.name).c_str()) {
            const auto actual_rgb = hsv_to_rgb(colour_case.hue, 1.0f, 1.0f);
            for (int channel_index = 0; channel_index < 3; ++channel_index) CHECK(std::fabs((actual_rgb[channel_index]) - (colour_case.rgb[channel_index])) < 1e-5f);
        }
    }
}

TEST_CASE("hue wraps in both directions")
{
    for (float hue : {-1.0f, 1.0f, 2.0f}) {
        const auto rgb = hsv_to_rgb(hue, 1.0f, 1.0f);
        CHECK(std::fabs((rgb[0]) - (1.0f)) < 1e-5f);
        CHECK(std::fabs((rgb[1]) - (0.0f)) < 1e-5f);
        CHECK(std::fabs((rgb[2]) - (0.0f)) < 1e-5f);
    }
}

TEST_CASE("zero saturation gives grey")
{
    for (float value : {0.0f, 0.4f, 1.0f}) {
        const auto rgb = hsv_to_rgb(0.2f, 0.0f, value);
        for (float channel : rgb) CHECK(std::fabs((channel) - (value)) < 1e-5f);
    }
}

TEST_CASE("saturation and brightness clamp")
{
    const auto white = hsv_to_rgb(0.0f, -1.0f, 2.0f);
    const auto red = hsv_to_rgb(0.0f, 2.0f, 1.0f);
    const auto black = hsv_to_rgb(0.0f, 1.0f, -1.0f);
    for (float channel : white) CHECK(std::fabs((channel) - (1.0f)) < 1e-5f);
    CHECK(std::fabs((red[0]) - (1.0f)) < 1e-5f);
    CHECK(std::fabs((red[1]) - (0.0f)) < 1e-5f);
    CHECK(std::fabs((red[2]) - (0.0f)) < 1e-5f);
    for (float channel : black) CHECK(std::fabs((channel) - (0.0f)) < 1e-5f);
}

TEST_CASE("unknown colour lookup reports failure")
{
    float hue = 0.25f;
    REQUIRE(!hue_from_name("not-a-colour", hue));
    CHECK(std::fabs((hue) - (0.25f)) < 1e-5f);
}

/* RGB expectations are independent of the implementation's HSV table. */
TEST_CASE("named palette produces the expected RGB values")
{
    for (const NamedCase &colour_case : named_colours) {
        SUBCASE((std::string("named palette RGB: ") + colour_case.name).c_str()) {
            std::array<float, 3> hsv;
            REQUIRE(hsv_from_name(colour_case.name, hsv));
            const auto rgb = hsv_to_rgb(hsv[0], hsv[1], hsv[2]);
            for (int channel_index = 0; channel_index < 3; ++channel_index) CHECK(std::fabs((rgb[channel_index]) - (colour_case.rgb[channel_index])) < 1e-5f);
        }
    }
}

TEST_CASE("pitch classes select the expected palette colours")
{
    for (int pitch = 0; pitch < 12; ++pitch) {
        const NamedCase colour_case = named_colours[pitch];
        SUBCASE(("pitch palette RGB for class " + std::to_string(pitch)).c_str()) {
            const auto rgb = hsv_to_rgb(hue_from_pitch_class(pitch), 1.0f, 1.0f);
            for (int channel_index = 0; channel_index < 3; ++channel_index) CHECK(std::fabs((rgb[channel_index]) - (colour_case.rgb[channel_index])) < 1e-5f);
        }
    }
}

TEST_CASE("pitch palette wraps negative and large indices")
{
    CHECK(std::fabs((hue_from_pitch_class(-1)) - (330.0f / 360.0f)) < 1e-5f);
    CHECK(std::fabs((hue_from_pitch_class(12)) - (0.0f)) < 1e-5f);
    CHECK(std::fabs((hue_from_pitch_class(30)) - (120.0f / 360.0f)) < 1e-5f);
}
