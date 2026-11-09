#pragma once

#include <array>
#include <iosfwd>

#include "def_types.hpp"

/* Frequency limits in Hz and the fixed amplitude gain for each band. */
struct BandRange { float lower_frequency_hz, upper_frequency_hz; float gain; };

inline constexpr std::array<BandRange, BandCount> DefaultBandRanges{{
    {   20.0f,   250.0f, 0.0060f },
    {  250.0f,  1500.0f, 0.0060f },
    { 1500.0f, 20000.0f, 0.0042f },
}};

/* Read bass, mid and treble entries of the form name={low, high}. All three are
 * required, in any order. Throws std::runtime_error on invalid input; no partially
 * loaded configuration is returned. */
std::array<BandRange, BandCount> read_band_config(std::istream &input,
                                               float max_frequency_hz);
