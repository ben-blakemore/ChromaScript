/* Capture speaker output and publish band amplitudes, loudness, tone height and
 * pitch-class estimates. */
#include "runtime/audio/audio_handler.hpp"
#include "runtime/audio/band_config.hpp"

#include <fftw3.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int FftSize    = 1024;   /* samples per analysis block (power of two) */
constexpr int SampleRate = 48000;

/* Unique real-to-complex FFT bins, from DC through Nyquist. */
constexpr int FftBins = FftSize / 2 + 1;

/* The chroma analyser consumes complete capture blocks. */
static_assert(ChromaBlockSize == FftSize,
              "chroma block size must match the capture block size");

/* Smoothing weights for the one-pole filter (REQ-RUN-003). */
constexpr float SmoothPrev = 0.35f;
constexpr float SmoothNew  = 0.65f;

/* Map this dBFS interval onto [0, 1], clamping quieter and louder input. */
constexpr float LoudnessFloorDb   = -55.0f;
constexpr float LoudnessCeilingDb = -8.0f;
constexpr float LoudnessRangeDb   = LoudnessCeilingDb - LoudnessFloorDb;

/* Frequency bounds for the power-weighted logarithmic spectral centroid (Lerch,
 * module 3.5, slide 8). */
constexpr float ToneHeightMinHz = 100.0f;
constexpr float ToneHeightMaxHz = 6000.0f;
/* Reference for Lerch's log2(f / f_ref). Its value cancels during the normalisation,
 * provided the same reference is used for both bounds. */
constexpr float ToneHeightReferenceHz = 1000.0f;

/* Ignore centroid bins below this fraction of the block's peak magnitude. */
constexpr float ToneHeightGate = 0.05f;

/* User frequency ranges are loaded once at startup. Gains remain fixed so changing a
 * band's limits does not also change its amplitude scaling. */
std::array<BandRange, BandCount> load_band_ranges()
{
    constexpr const char *BandConfigPath = "config/band_values.cfg";
    std::ifstream input(BandConfigPath);
    if (!input) {
        std::fprintf(stderr, "audio: cannot open %s; using default band ranges\n", BandConfigPath);
        return DefaultBandRanges;
    }
    try {
        return read_band_config(input, SampleRate / 2.0f);
    } catch (const std::runtime_error &error) {
        std::fprintf(stderr, "audio: %s: %s; using default band ranges\n",
                     BandConfigPath, error.what());
        return DefaultBandRanges;
    }
}

/* Half-open bin range [start_bin, end_bin), excluding DC and capped at the FFT
 * output size. */
struct BinRange { int start_bin, end_bin; };

BinRange bins_for(const BandRange &band_range)
{
    constexpr float BinWidthHz = static_cast<float>(SampleRate) / FftSize;
    const int start_bin = std::max(1, static_cast<int>(band_range.lower_frequency_hz / BinWidthHz));
    const int end_bin = std::min(FftBins, static_cast<int>(band_range.upper_frequency_hz / BinWidthHz));
    return { start_bin, std::max(start_bin, end_bin) };
}

/* Use PULSE_SOURCE when set; otherwise capture the default output sink's monitor. An
 * empty name lets the server choose. */
std::string monitor_device()
{
    if (const char *source_override = std::getenv("PULSE_SOURCE"); source_override && *source_override) {
        return source_override;
    }

    std::string device;
    if (FILE *sink_pipe = popen("pactl get-default-sink", "r")) {
        char sink_name[200] = {0};
        if (std::fgets(sink_name, sizeof sink_name, sink_pipe)) {
            sink_name[std::strcspn(sink_name, "\n")] = '\0';   /* strip newline */
            device = std::string(sink_name) + ".monitor";
        }
        pclose(sink_pipe);
    }
    return device;
}

}  // namespace

AudioHandler::AudioHandler()
{
    /* Start the thread last, so every member it touches is already initialised. */
    thread_ = std::thread(&AudioHandler::capture_loop, this);
}

AudioHandler::~AudioHandler()
{
    /* Request shutdown and wait for the current blocking capture read to finish. */
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

float AudioHandler::band(Band band) const
{
    /* Protect the band value while copying it. */
    std::lock_guard<std::mutex> lock(bands_mutex_);
    return bands_[band];
}

float AudioHandler::loudness() const
{
    return loudness_.load(std::memory_order_relaxed);
}

/* The accumulated pitch class, normalised independently of the visual palette. The
 * DSL translates this to an explicit Marcotone hue when it drives colour. */
float AudioHandler::main_tone() const
{
    return static_cast<float>(main_class_.load(std::memory_order_relaxed))
           / static_cast<float>(PitchClassCount);
}

/* Whether a pitch class has been selected. */
bool AudioHandler::has_tone() const
{
    return has_tone_.load(std::memory_order_relaxed);
}

/* Read the logarithmic spectral centroid published by the capture thread. */
float AudioHandler::tone_height() const
{
    return tone_height_.load(std::memory_order_relaxed);
}

void AudioHandler::publish(const std::array<float, BandCount> &band_values)
{
    std::lock_guard<std::mutex> lock(bands_mutex_);
    bands_ = band_values;
}

void AudioHandler::capture_loop()
{
    const auto band_ranges = load_band_ranges();

    /* Ask for mono float samples at our sample rate. PulseAudio/PipeWire down-mixes
     * the stereo monitor to mono for us. */
    pa_sample_spec sample_specification{};
    sample_specification.format   = PA_SAMPLE_FLOAT32NE;
    sample_specification.rate     = SampleRate;
    sample_specification.channels = 1;

    const std::string device = monitor_device();
    const char *device_name = device.empty() ? nullptr : device.c_str();

    /* Request one block per read and at most two queued blocks. Unused settings
     * retain the server defaults. */
    const std::uint32_t block_bytes = FftSize * sizeof(float);
    pa_buffer_attr buffer_attributes{};
    buffer_attributes.maxlength = 2 * block_bytes;
    buffer_attributes.fragsize  = block_bytes;
    buffer_attributes.tlength   = static_cast<std::uint32_t>(-1);
    buffer_attributes.prebuf    = static_cast<std::uint32_t>(-1);
    buffer_attributes.minreq    = static_cast<std::uint32_t>(-1);

    int pulse_error = 0;
    pa_simple *stream = pa_simple_new(nullptr, "dsl", PA_STREAM_RECORD, device_name,
                                      "capture", &sample_specification, nullptr,
                                      &buffer_attributes, &pulse_error);
    if (!stream) {
        std::fprintf(stderr, "audio: cannot open capture stream (%s)\n",
                     pa_strerror(pulse_error));
        return;
    }

    std::vector<float> samples(FftSize);
    std::array<float, BandCount> smoothed_bands{};
    float smoothed_loudness = 0.0f;
    float smoothed_tone_height   = 0.5f;   /* midpoint, matching the published default */

    /* Convert configured frequency ranges to bin ranges once per stream. */
    std::array<BinRange, BandCount> band_bins{};
    for (int band_index = 0; band_index < BandCount; ++band_index) {
        band_bins[band_index] = bins_for(band_ranges[band_index]);
    }

    /* Precompute the centroid's bin range and logarithmic normalisation bounds,
     * excluding DC. */
    constexpr float BinWidthHz = static_cast<float>(SampleRate) / FftSize;
    const int centroid_first_bin = std::max(1, static_cast<int>(ToneHeightMinHz / BinWidthHz));
    const int centroid_last_bin = std::min(FftBins - 1,
                                     static_cast<int>(ToneHeightMaxHz / BinWidthHz));
    const float minimum_log_frequency = std::log2(ToneHeightMinHz / ToneHeightReferenceHz);
    const float maximum_log_frequency = std::log2(ToneHeightMaxHz / ToneHeightReferenceHz);

    /* FFTW provides aligned buffers for its transform. */
    float         *fft_input_samples   = fftwf_alloc_real(FftSize);
    fftwf_complex *frequency_bins = fftwf_alloc_complex(FftBins);
    if (!fft_input_samples || !frequency_bins) {
        std::fprintf(stderr, "audio: cannot allocate FFT buffers\n");
        fftwf_free(fft_input_samples);
        fftwf_free(frequency_bins);
        pa_simple_free(stream);
        return;
    }

    /* Plan once before writing samples: FFTW_MEASURE overwrites the buffers while
     * selecting an algorithm. */
    fftwf_plan fft_plan = fftwf_plan_dft_r2c_1d(FftSize, fft_input_samples, frequency_bins, FFTW_MEASURE);
    if (!fft_plan) {
        std::fprintf(stderr, "audio: cannot create FFT plan\n");
        fftwf_free(fft_input_samples);
        fftwf_free(frequency_bins);
        pa_simple_free(stream);
        return;
    }

    /* The Hann window is the same every block, so build it once rather than
     * recomputing a cosine per sample per block. */
    std::vector<float> window(FftSize);
    for (int sample_index = 0; sample_index < FftSize; ++sample_index) {
        window[sample_index] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI)
                                            * sample_index / (FftSize - 1)));
    }

    while (running_) {
        if (pa_simple_read(stream, samples.data(),
                           samples.size() * sizeof(float), &pulse_error) < 0) {
            std::fprintf(stderr, "audio: read failed (%s)\n", pa_strerror(pulse_error));
            break;
        }

        /* Feed raw samples to chroma before applying the short FFT's window; chroma
         * maintains its own longer window. */
        chroma_.push(samples.data(), FftSize);
        main_class_.store(chroma_.main_class(), std::memory_order_relaxed);
        has_tone_.store(chroma_.has_tone(), std::memory_order_relaxed);

        /* Measure RMS before windowing, then map dBFS to [0, 1]. Handle silence
         * before taking the logarithm. */
        float sample_square_sum = 0.0f;
        for (int sample_index = 0; sample_index < FftSize; ++sample_index) {
            sample_square_sum += samples[sample_index] * samples[sample_index];
        }
        const float rms_amplitude = std::sqrt(sample_square_sum / static_cast<float>(FftSize));

        float normalised_loudness = 0.0f;
        if (rms_amplitude > 0.0f) {
            const float level_dbfs = 20.0f * std::log10(rms_amplitude);
            normalised_loudness = (level_dbfs - LoudnessFloorDb) / LoudnessRangeDb;
            if (normalised_loudness < 0.0f) normalised_loudness = 0.0f;   /* at or below the floor */
            if (normalised_loudness > 1.0f) normalised_loudness = 1.0f;   /* at or above the ceiling */
        }

        /* Smooth loudness with the same one-pole filter as the bands. */
        smoothed_loudness = SmoothPrev * smoothed_loudness + SmoothNew * normalised_loudness;
        loudness_.store(smoothed_loudness, std::memory_order_relaxed);

        /* Apply the Hann window before the real-to-complex transform. */
        for (int sample_index = 0; sample_index < FftSize; ++sample_index) {
            fft_input_samples[sample_index] = samples[sample_index] * window[sample_index];
        }
        fftwf_execute(fft_plan);

        /* Reduce each band's bins to a single amplitude, scale, clamp, smooth. */
        std::array<float, BandCount> band_values{};
        for (int band_index = 0; band_index < BandCount; ++band_index) {
            float magnitude_sum = 0.0f;
            for (int bin_index = band_bins[band_index].start_bin;
                 bin_index < band_bins[band_index].end_bin; ++bin_index) {
                float real_part = frequency_bins[bin_index][0], imaginary_part = frequency_bins[bin_index][1];
                magnitude_sum += std::sqrt(real_part*real_part + imaginary_part*imaginary_part);
            }
            float band_amplitude = magnitude_sum * band_ranges[band_index].gain;
            if (band_amplitude > 1.0f) band_amplitude = 1.0f;
            /* Blend the previous band amplitude with the current one. */
            smoothed_bands[band_index] = SmoothPrev * smoothed_bands[band_index] + SmoothNew * band_amplitude;
            band_values[band_index] = smoothed_bands[band_index];
        }
        publish(band_values);

        /* Reuse the band FFT for tone height: find the peak, then accumulate the
         * gated logarithmic centroid. */
        float peak_magnitude = 0.0f;
        for (int bin_index = centroid_first_bin; bin_index <= centroid_last_bin; ++bin_index) {
            const float real_part = frequency_bins[bin_index][0],
                        imaginary_part = frequency_bins[bin_index][1];
            const float magnitude = std::sqrt(real_part*real_part + imaginary_part*imaginary_part);
            if (magnitude > peak_magnitude) peak_magnitude = magnitude;
        }

        if (peak_magnitude > 0.0f) {
            const float magnitude_threshold = peak_magnitude * ToneHeightGate;
            float weighted_log_frequency_sum = 0.0f, spectral_power_sum = 0.0f;
            for (int bin_index = centroid_first_bin; bin_index <= centroid_last_bin; ++bin_index) {
                const float real_part = frequency_bins[bin_index][0],
                            imaginary_part = frequency_bins[bin_index][1];
                const float magnitude = std::sqrt(real_part*real_part + imaginary_part*imaginary_part);
                if (magnitude < magnitude_threshold) continue;
                // Lerch: weight log2(f / f_ref) by spectral power |X|^2.
                const float spectral_power = real_part*real_part + imaginary_part*imaginary_part;
                const float frequency = static_cast<float>(bin_index) * BinWidthHz;
                weighted_log_frequency_sum += std::log2(frequency / ToneHeightReferenceHz) * spectral_power;
                spectral_power_sum    += spectral_power;
            }

            if (spectral_power_sum > 0.0f) {
                /* Normalise the centroid between the configured bounds and clamp to
                 * [0, 1]. */
                float normalised_tone_height =
                    (weighted_log_frequency_sum / spectral_power_sum - minimum_log_frequency)
                    / (maximum_log_frequency - minimum_log_frequency);
                if (normalised_tone_height < 0.0f) normalised_tone_height = 0.0f;
                if (normalised_tone_height > 1.0f) normalised_tone_height = 1.0f;

                /* Smooth valid centroid readings; silence holds the previous tone
                 * height. */
                smoothed_tone_height = SmoothPrev * smoothed_tone_height + SmoothNew * normalised_tone_height;
                tone_height_.store(smoothed_tone_height, std::memory_order_relaxed);
            }
        }
    }

    fftwf_destroy_plan(fft_plan);
    fftwf_free(fft_input_samples);
    fftwf_free(frequency_bins);
    pa_simple_free(stream);
}
