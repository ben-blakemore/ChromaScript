#include "doctest/doctest.h"

#include <cmath>
#include <cstdio>
#include <string>
#include "runtime/audio/chroma.hpp"
#include <cstdint>

namespace {

constexpr int SampleRate = 48000;

std::string amplitude_label(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.3g", static_cast<double>(value));
    return buffer;
}

float noise_sample(std::uint32_t &noise_state)
{
    noise_state = 1664525u * noise_state + 1013904223u;
    return static_cast<float>(2.0 * static_cast<double>(noise_state) / 4294967296.0 - 1.0);
}

void feed_signal(ChromaAnalyser &analyser, int midi_note, float amplitude,
                 float noise_amplitude = 0.0f, std::uint32_t noise_seed = 1)
{
    std::array<float, ChromaBlockSize> samples{};
    const double frequency_hz = 440.0 * std::pow(2.0, (midi_note - 69) / 12.0);
    const double pi = std::acos(-1.0);
    /* A little over two seconds, allowing the accumulator to settle. */
    for (int block_index = 0; block_index < 100; ++block_index) {
        for (int sample_index = 0; sample_index < ChromaBlockSize; ++sample_index) {
            const double phase = 2 * pi * frequency_hz * (block_index * ChromaBlockSize + sample_index) / SampleRate;
            samples[sample_index] = amplitude * std::sin(phase) + noise_amplitude * noise_sample(noise_seed);
        }
        analyser.push(samples.data(), samples.size());
    }
}

void check_pitch(const ChromaAnalyser &analyser, int midi_note)
{
    INFO("MIDI " << midi_note << ", expected class " << midi_note % 12);
    REQUIRE(analyser.has_tone());
    CHECK(analyser.main_class() == midi_note % 12);
}

} // namespace

TEST_CASE("silence does not establish a tone")
{
    ChromaAnalyser analyser;
    std::array<float, ChromaBlockSize> silence{};
    for (int block_index = 0; block_index < 50; ++block_index) analyser.push(silence.data(), silence.size());
    REQUIRE(!analyser.has_tone());
}

TEST_CASE("incomplete analysis window does not establish a tone")
{
    ChromaAnalyser analyser;
    std::array<float, ChromaBlockSize> samples{};
    samples.fill(0.1f);
    analyser.push(samples.data(), samples.size());
    REQUIRE(!analyser.has_tone());
}

TEST_CASE("synthetic C4 to B4 tones establish the correct pitch class")
{
    for (int midi_note = 60; midi_note < 72; ++midi_note) {
        SUBCASE(("synthetic pitch class for MIDI " + std::to_string(midi_note)).c_str()) {
            ChromaAnalyser analyser;
            std::array<float, ChromaBlockSize> samples{};
            const double frequency_hz = 440.0 * std::pow(2.0, (midi_note - 69) / 12.0);
            const double pi = std::acos(-1.0);
            for (int block_index = 0; block_index < 50; ++block_index) {
                for (int sample_index = 0; sample_index < ChromaBlockSize; ++sample_index) {
                    samples[sample_index] = 0.1 * std::sin(2 * pi * frequency_hz * (block_index * ChromaBlockSize + sample_index) / 48000);
                }
                analyser.push(samples.data(), samples.size());
            }
            REQUIRE(analyser.has_tone());
            REQUIRE(analyser.main_class() == midi_note % 12);
        }
    }
}

TEST_CASE("peakiness distinguishes uniform from concentrated chroma")
{
    std::array<float, PitchClassCount> uniform;
    uniform.fill(1.0f);
    std::array<float, PitchClassCount> concentrated{};
    concentrated[5] = 1.0f;
    CHECK(std::fabs((ChromaAnalyser::peakiness(uniform)) - (1.0f)) < 1e-5f);
    CHECK(std::fabs((ChromaAnalyser::peakiness(concentrated)) - (12.0f)) < 1e-5f);
}

// Pitch-class accuracy across registers and noise conditions.
TEST_CASE("low-register tones establish the correct pitch class")
{
    for (int midi_note = 36; midi_note < 60; ++midi_note) {
        for (float amplitude : {0.001f, 0.01f, 0.1f}) {
            SUBCASE(("low-register MIDI " + std::to_string(midi_note) + " amplitude "
                       + amplitude_label(amplitude)).c_str()) {
                ChromaAnalyser analyser;
                feed_signal(analyser, midi_note, amplitude);
                check_pitch(analyser, midi_note);
            }
        }
    }
}

TEST_CASE("noise does not establish a tone")
{
    for (float amplitude : {1e-7f, 1e-4f, 0.05f}) {
        for (std::uint32_t noise_seed : {1u, 17u, 12345u}) {
            SUBCASE(("noise must not establish a tone: amplitude "
                       + amplitude_label(amplitude) + " seed "
                       + std::to_string(noise_seed)).c_str()) {
                ChromaAnalyser analyser;
                feed_signal(analyser, 60, 0.0f, amplitude, noise_seed);
                INFO("observed class " << analyser.main_class() << ", peakiness "
                     << ChromaAnalyser::peakiness(analyser.accumulated()));
                CHECK(!analyser.has_tone());
            }
        }
    }
}

TEST_CASE("quiet tones and tones with noise remain detectable")
{
    for (int midi_note : {48, 57, 60}) {
        SUBCASE(("quiet tone remains detectable: MIDI " + std::to_string(midi_note)).c_str()) {
            ChromaAnalyser analyser;
            feed_signal(analyser, midi_note, 0.001f);
            check_pitch(analyser, midi_note);
        }
        for (float noise : {0.001f, 0.01f}) {
            SUBCASE(("tone with noise: MIDI " + std::to_string(midi_note) + " noise "
                       + amplitude_label(noise)).c_str()) {
                ChromaAnalyser analyser;
                feed_signal(analyser, midi_note, 0.05f, noise);
                check_pitch(analyser, midi_note);
            }
        }
    }
}

TEST_CASE("noise after a tone must not invent a different pitch")
{
    ChromaAnalyser analyser;
    feed_signal(analyser, 69, 0.1f);
    check_pitch(analyser, 69);
    feed_signal(analyser, 60, 0.0f, 0.01f, 17u);
    REQUIRE((!analyser.has_tone() || analyser.main_class() == 9));
}
