#include "runtime/audio/chroma.hpp"

#include <fftw3.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr int SampleRate = 48000;

/* Reference pitch: A4 = 440 Hz, MIDI note 69. Equal temperament throughout, which is
 * what Maryon's twelve named tones assume. */
constexpr float ReferenceFrequencyHz   = 440.0f;
constexpr int   ReferenceMidiNote = 69;

/* Retain 65–2,000 Hz for chroma pooling. Low-frequency bin spacing limits pitch
 * discrimination; the upper bound excludes high-frequency content. */
constexpr float MinimumFrequencyHz = 65.0f;
constexpr float MaximumFrequencyHz = 2000.0f;

/* Exponential accumulation time constant in seconds; controls stability versus
 * responsiveness. */
constexpr float AccumulationTimeConstantSeconds = 0.5f;

/* A challenger must exceed the selected class by this fraction before replacing it. */
constexpr float HysteresisMargin = 0.15f;

/* Minimum pooled magnitude required to update the chroma estimate; this is not a
 * tonality test. */
constexpr float SilenceThreshold = 1e-6f;

}  // namespace

/* FFTW's types kept out of the header: only this file needs them. */
struct ChromaAnalyser::Fft {
    float         *input_samples   = nullptr;
    fftwf_complex *output_bins  = nullptr;
    fftwf_plan     plan = nullptr;
};

ChromaAnalyser::ChromaAnalyser()
    : fft_(new Fft)
{
    /* Hann window, precomputed once: the same coefficients apply to every transform. */
    for (int sample_index = 0; sample_index < ChromaFftSize; ++sample_index) {
        hann_window_[sample_index] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI)
                                           * sample_index / (ChromaFftSize - 1)));
    }

    /* Assign each retained bin to the nearest MIDI pitch class. Pool magnitudes
     * rather than squared magnitudes. */
    for (int bin_index = 0; bin_index <= ChromaFftSize / 2; ++bin_index) {
        const float frequency_hz = static_cast<float>(bin_index) * SampleRate / ChromaFftSize;
        if (frequency_hz < MinimumFrequencyHz || frequency_hz > MaximumFrequencyHz) {
            bin_map_[bin_index] = { 0, 0.0f };
            continue;
        }
        const float midi_position =
            12.0f * std::log2(frequency_hz / ReferenceFrequencyHz) + ReferenceMidiNote;
        const int   midi_note  = static_cast<int>(std::lround(midi_position));
        bin_map_[bin_index] = { ((midi_note % 12) + 12) % 12, 1.0f };
    }

    fft_->input_samples  = fftwf_alloc_real(ChromaFftSize);
    fft_->output_bins = fftwf_alloc_complex(ChromaFftSize / 2 + 1);
    if (!fft_->input_samples || !fft_->output_bins) {
        std::fprintf(stderr, "chroma: cannot allocate FFT buffers\n");
        return;
    }

    /* Build the plan before filling the buffers: FFTW_MEASURE overwrites them. */
    fft_->plan = fftwf_plan_dft_r2c_1d(ChromaFftSize, fft_->input_samples, fft_->output_bins,
                                       FFTW_MEASURE);
    if (!fft_->plan) {
        std::fprintf(stderr, "chroma: cannot create FFT plan\n");
    }
}

ChromaAnalyser::~ChromaAnalyser()
{
    if (fft_) {
        if (fft_->plan) fftwf_destroy_plan(fft_->plan);
        fftwf_free(fft_->input_samples);
        fftwf_free(fft_->output_bins);
        delete fft_;
    }
}

void ChromaAnalyser::push(const float *samples, int sample_count)
{
    if (!fft_ || !fft_->plan) {
        return;
    }

    /* Append samples to the ring buffer, overwriting the oldest values. */
    for (int sample_index = 0; sample_index < sample_count; ++sample_index) {
        sample_window_[write_position_] = samples[sample_index];
        write_position_ = (write_position_ + 1) % ChromaFftSize;
    }
    if (buffered_sample_count_ < ChromaFftSize) {
        buffered_sample_count_ += sample_count;
    }

    /* Wait for a complete window before analysing. */
    if (buffered_sample_count_ >= ChromaFftSize) {
        analyse();
    }
}

void ChromaAnalyser::analyse()
{
    /* Read samples chronologically from the ring buffer and apply the Hann window. */
    for (int sample_index = 0; sample_index < ChromaFftSize; ++sample_index) {
        const int ring_buffer_index = (write_position_ + sample_index) % ChromaFftSize;
        fft_->input_samples[sample_index] = sample_window_[ring_buffer_index] * hann_window_[sample_index];
    }
    fftwf_execute(fft_->plan);

    /* Fold the spectrum into twelve classes. */
    std::array<float, PitchClassCount> chroma{};
    for (int bin_index = 0; bin_index <= ChromaFftSize / 2; ++bin_index) {
        const float bin_weight = bin_map_[bin_index].weight;
        if (bin_weight == 0.0f) {
            continue;
        }
        const float real_part = fft_->output_bins[bin_index][0],
                    imaginary_part = fft_->output_bins[bin_index][1];
        chroma[bin_map_[bin_index].pitch_class] +=
            bin_weight * std::sqrt(real_part * real_part + imaginary_part * imaginary_part);
    }

    /* Normalise class contributions so accumulation is independent of overall
     * magnitude. */
    float total_magnitude = 0.0f;
    for (float class_magnitude : chroma) {
        total_magnitude += class_magnitude;
    }
    if (total_magnitude <= SilenceThreshold) {
        /* Silence preserves the accumulated estimate and selected class. */
        return;
    }
    for (float &class_magnitude : chroma) {
        class_magnitude /= total_magnitude;
    }
    instantaneous_chroma_ = chroma;

    /* Derive the averaging weight from the block duration and time constant. */
    const float block_duration_seconds    = static_cast<float>(ChromaBlockSize) / SampleRate;
    const float accumulation_weight =
        1.0f - std::exp(-block_duration_seconds / AccumulationTimeConstantSeconds);
    for (int pitch_class_index = 0; pitch_class_index < PitchClassCount; ++pitch_class_index) {
        accumulated_chroma_[pitch_class_index] +=
            (chroma[pitch_class_index] - accumulated_chroma_[pitch_class_index])
            * accumulation_weight;
    }

    /* Select the strongest class, subject to the hysteresis margin. */
    int   strongest_pitch_class     = 0;
    float strongest_class_magnitude = accumulated_chroma_[0];
    for (int pitch_class_index = 1; pitch_class_index < PitchClassCount; ++pitch_class_index) {
        if (accumulated_chroma_[pitch_class_index] > strongest_class_magnitude) {
            strongest_class_magnitude = accumulated_chroma_[pitch_class_index];
            strongest_pitch_class = pitch_class_index;
        }
    }

    if (!has_selected_pitch_class_) {
        selected_pitch_class_ = strongest_pitch_class;
        has_selected_pitch_class_  = true;
    } else if (strongest_pitch_class != selected_pitch_class_
               && strongest_class_magnitude >
                   accumulated_chroma_[selected_pitch_class_] * (1.0f + HysteresisMargin)) {
        selected_pitch_class_ = strongest_pitch_class;
    }
}

int ChromaAnalyser::main_class() const
{
    return selected_pitch_class_;
}

bool ChromaAnalyser::has_tone() const
{
    return has_selected_pitch_class_;
}

float ChromaAnalyser::peakiness(const std::array<float, PitchClassCount> &class_energies)
{
    float total_energy = 0.0f, peak_energy = 0.0f;
    for (float class_energy : class_energies) {
        total_energy += class_energy;
        if (class_energy > peak_energy) peak_energy = class_energy;
    }
    if (total_energy <= 0.0f) {
        return 1.0f;
    }
    return peak_energy / (total_energy / PitchClassCount);
}
