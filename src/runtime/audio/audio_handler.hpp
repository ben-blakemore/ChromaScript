/* Capture and analyse speaker output on a worker thread. Published features are read
 * independently by the renderer. Construction starts capture; destruction stops and
 * joins it. */
#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <thread>

#include "def_types.hpp"
#include "runtime/audio/chroma.hpp"

/* Forward-declared so the PulseAudio headers stay out of this interface; only the
 * implementation needs them. */
struct pa_simple;

class AudioHandler {
public:
    /* Start capture. A device failure is reported and leaves the initial feature
     * values unchanged. */
    AudioHandler();
    ~AudioHandler();

    /* Not copyable or movable: it owns a running thread that captures `this`. */
    AudioHandler(const AudioHandler &) = delete;
    AudioHandler &operator=(const AudioHandler &) = delete;
    AudioHandler(AudioHandler &&) = delete;
    AudioHandler &operator=(AudioHandler &&) = delete;

    /* The current amplitude of one band, in [0, 1] (REQ-RUN-005). Called from the
     * render thread, once per bound property per frame. */
    float band(Band band) const;

    /* Overall RMS level mapped from decibels to [0, 1] for size and brightness
     * bindings. */
    float loudness() const;

    /* Selected pitch class divided by twelve: C = 0, B = 11/12. The DSL applies the
     * colour palette when this drives hue. */
    float main_tone() const;

    /* Whether the analyser has selected a pitch class; false before its first usable
     * window. */
    bool has_tone() const;

    /* Power-weighted logarithmic spectral centroid, normalised to [0, 1]. Holds its
     * previous value during silence; initially 0.5. */
    float tone_height() const;

private:
    void capture_loop();

    /* Replace all band values under one lock. Separate band() calls can still span
     * different updates. */
    void publish(const std::array<float, BandCount> &band_values);

    std::array<float, BandCount> bands_{};
    mutable std::mutex           bands_mutex_;

    /* Independent scalar features use atomics; they do not form a consistent
     * snapshot with the bands. */
    std::atomic<float> loudness_{0.0f};
    std::atomic<float> tone_height_{0.5f};

    /* Used only by the capture thread; the renderer reads the published result. */
    ChromaAnalyser chroma_;

    /* Publish the selected class and selection flag as independent atomics. */
    std::atomic<int>  main_class_{0};
    std::atomic<bool> has_tone_{false};

    /* Synchronises the destructor's stop request with the capture loop. */
    std::atomic<bool> running_{true};

    std::thread thread_;
};
