#include "doctest/doctest.h"
#include "runtime/audio/band_config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

TEST_CASE("user band configuration is valid")
{
    std::ifstream input("config/band_values.cfg");
    REQUIRE(input.is_open());
    CHECK_NOTHROW(read_band_config(input, 24000.0f));
}

TEST_CASE("custom frequency ranges accept comments whitespace and any band order")
{
    std::istringstream input(
        "# Custom cutoffs\r\n\r\n"
        " treble = { 3200.5, 24000 } # upper range\r\n"
        "bass={0, 175.5}\n"
        "\tmid\t=\t{175.5, 3200.5}");
    const auto ranges = read_band_config(input, 24000.0f);
    CHECK(ranges[BandBass].lower_frequency_hz == 0.0f);
    CHECK(ranges[BandBass].upper_frequency_hz == 175.5f);
    CHECK(ranges[BandMid].lower_frequency_hz == 175.5f);
    CHECK(ranges[BandMid].upper_frequency_hz == 3200.5f);
    CHECK(ranges[BandTreble].lower_frequency_hz == 3200.5f);
    CHECK(ranges[BandTreble].upper_frequency_hz == 24000.0f);
    for (int band_index = 0; band_index < BandCount; ++band_index) {
        CHECK(ranges[band_index].gain == DefaultBandRanges[band_index].gain);
    }
}

TEST_CASE("invalid frequency configurations are rejected")
{
    for (const char *entry : {
            "bass={-1, 250}", "bass={250, 250}", "bass={300, 250}",
            "bass={20, 24001}", "bass={20, 1e100}", "bass={nan, 250}",
            "bass={20, inf}", "bass={20 250}", "bass=[20, 250]",
            "bass={20, 250, 300}", "bass={20, 250} trailing",
            "bass={20, 250", "bass {20, 250}", "={20, 250}",
            "unknown={20, 250}", "mid={20, 250}"}) {
        INFO(entry);
        std::istringstream input(std::string(entry)
                                + "\nmid={250, 1500}\ntreble={1500, 20000}\n");
        CHECK_THROWS_AS(read_band_config(input, 24000.0f), std::runtime_error);
    }
}

TEST_CASE("empty incomplete and duplicate band configurations are rejected")
{
    for (const char *text : {
            "", "# Only a comment\n", "bass={20, 250}\nmid={250, 1500}\n",
            "bass={20, 250}\nbass={30, 200}\nmid={250, 1500}\ntreble={1500, 20000}"}) {
        INFO(text);
        std::istringstream input(text);
        CHECK_THROWS_AS(read_band_config(input, 24000.0f), std::runtime_error);
    }
}

TEST_CASE("frequency validation respects the capture Nyquist limit")
{
    std::istringstream input("bass={20, 250}\nmid={250, 1500}\ntreble={1500, 22000}");
    CHECK_THROWS_AS(read_band_config(input, 16000.0f), std::runtime_error);
}
