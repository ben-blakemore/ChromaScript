#include "runtime/audio/band_config.hpp"

#include <cmath>
#include <istream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

std::array<BandRange, BandCount> read_band_config(std::istream &input,
                                               float max_frequency_hz)
{
    auto band_ranges = DefaultBandRanges;
    std::array<bool, BandCount> bands_seen{};
    std::string config_line;
    int line_number = 0;

    while (std::getline(input, config_line)) {
        ++line_number;
        const auto report_error = [&](const std::string &message) {
            throw std::runtime_error("line " + std::to_string(line_number)
                                     + ": " + message);
        };

        const auto comment_position = config_line.find('#');
        if (comment_position != std::string::npos) config_line.erase(comment_position);
        if (config_line.find_first_not_of(" \t\r") == std::string::npos) continue;

        const auto equals_position = config_line.find('=');
        if (equals_position == std::string::npos) report_error("expected name={low, high}");

        const std::string name_field = config_line.substr(0, equals_position);
        const auto name_start = name_field.find_first_not_of(" \t\r");
        if (name_start == std::string::npos) report_error("missing band name");

        const std::string band_name = name_field.substr(
            name_start, name_field.find_last_not_of(" \t\r") - name_start + 1);

        int band_index = -1;
        if (band_name == "bass") band_index = BandBass;
        else if (band_name == "mid") band_index = BandMid;
        else if (band_name == "treble") band_index = BandTreble;
        else report_error("unknown band '" + band_name + "'");
        if (bands_seen[band_index]) report_error("duplicate band '" + band_name + "'");

        std::istringstream range_values(config_line.substr(equals_position + 1));
        range_values.imbue(std::locale::classic());
        char opening_brace, separator, closing_brace;
        float lower_frequency_hz, upper_frequency_hz;
        if (!(range_values >> opening_brace >> lower_frequency_hz
                           >> separator >> upper_frequency_hz >> closing_brace)
            || opening_brace != '{' || separator != ',' || closing_brace != '}') {
            report_error("expected name={low, high}");
        }

        range_values >> std::ws;
        if (!range_values.eof()) report_error("unexpected text after frequency range");

        if (!std::isfinite(lower_frequency_hz) || !std::isfinite(upper_frequency_hz)
            || lower_frequency_hz < 0.0f || lower_frequency_hz >= upper_frequency_hz
            || upper_frequency_hz > max_frequency_hz) {
            report_error("frequencies must satisfy 0 <= low < high <= "
                 + std::to_string(max_frequency_hz) + " Hz");
        }
        
        band_ranges[band_index].lower_frequency_hz = lower_frequency_hz;
        band_ranges[band_index].upper_frequency_hz = upper_frequency_hz;
        bands_seen[band_index] = true;
    }
    if (input.bad() || (input.fail() && !input.eof())) {
        throw std::runtime_error("cannot read band configuration");
    }
    for (bool band_present : bands_seen) {
        if (!band_present) throw std::runtime_error("bass, mid and treble are all required");
    }
    return band_ranges;
}
