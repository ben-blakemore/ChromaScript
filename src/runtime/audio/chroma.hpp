/* Estimate a prevailing pitch class from live audio. Fold each FFT into twelve
 * classes, then smooth their contributions before selecting the strongest class. */
#pragma once

#include <array>

/* Longer than the band FFT for finer frequency spacing: 5.86 Hz bins over 170.67 ms
 * at 48 kHz. Low-register semitones can still be closer than one bin. */
constexpr int ChromaFftSize = 8192;

/* Samples between overlapping transforms: one capture block, or 21.33 ms at 48 kHz. */
constexpr int ChromaBlockSize = 1024;

/* Number of pitch classes: the twelve semitones of the chromatic scale, which is
 * what Maryon's table is indexed by. */
constexpr int PitchClassCount = 12;

class ChromaAnalyser {
    /* Allow diagnostic probes to inspect the analyser's internal state. */
    friend struct ChromaProbe;

public:
    ChromaAnalyser();
    ~ChromaAnalyser();

    /* Not copyable or movable: it owns FFTW plans and their aligned buffers. */
    ChromaAnalyser(const ChromaAnalyser &) = delete;
    ChromaAnalyser &operator=(const ChromaAnalyser &) = delete;
    ChromaAnalyser(ChromaAnalyser &&) = delete;
    ChromaAnalyser &operator=(ChromaAnalyser &&) = delete;

    /* Append sample_count samples and analyse once the window is full. Capture
     * supplies ChromaBlockSize samples per call. */
    void push(const float *samples, int sample_count);

    /* Selected class, C = 0 through B = 11. Use has_tone() to distinguish startup
     * from a selected C. */
    int main_class() const;

    /* Whether a class has been selected. The selection is retained during silence. */
    bool has_tone() const;

    /* Expose the accumulated and latest normalised chroma vectors for diagnostics. */
    const std::array<float, PitchClassCount> &accumulated() const { return accumulated_chroma_; }
    const std::array<float, PitchClassCount> &instantaneous() const { return instantaneous_chroma_; }

    /* Strongest class divided by the mean. A uniform vector gives 1; larger values
     * indicate concentration, not guaranteed tonality. */
    static float peakiness(const std::array<float, PitchClassCount> &class_magnitude);

private:
    /* Fold the current spectrum into a chroma vector and fold that into the running
     * average. */
    void analyse();

    /* Precomputed pitch class and weight for each FFT bin. */
    struct BinMap { int pitch_class; float weight; };
    std::array<BinMap, ChromaFftSize / 2 + 1> bin_map_{};

    /* Ring buffer of the latest ChromaFftSize samples. */
    std::array<float, ChromaFftSize> sample_window_{};
    int  write_position_ = 0;      /* where the next sample goes */
    int  buffered_sample_count_    = 0;      /* samples held, until the window is full */

    /* Precomputed Hann coefficients reduce spectral leakage before each FFT. */
    std::array<float, ChromaFftSize> hann_window_{};

    /* Exponential moving average of normalised chroma vectors. */
    std::array<float, PitchClassCount> accumulated_chroma_{};

    /* Latest normalised vector, retained for diagnostics. */
    std::array<float, PitchClassCount> instantaneous_chroma_{};

    /* Selected class, retained until a challenger exceeds the hysteresis margin. */
    int   selected_pitch_class_ = 0;
    bool  has_selected_pitch_class_  = false;

    /* Keep FFTW types private to chroma.cpp. */
    struct Fft;
    Fft *fft_ = nullptr;
};
