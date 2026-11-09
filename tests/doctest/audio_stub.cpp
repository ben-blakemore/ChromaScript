/* Linked only into the test executable, in place of live PulseAudio capture.
 * The real DSL resolves these deterministic readings through its normal API. */
#include "runtime/audio/audio_handler.hpp"

AudioHandler::AudioHandler() = default;
AudioHandler::~AudioHandler() = default;

float AudioHandler::band(Band band) const
{
    return 0.1f * (static_cast<int>(band) + 1);
}

float AudioHandler::loudness() const { return 0.4f; }
float AudioHandler::tone_height() const { return 0.6f; }
float AudioHandler::main_tone() const { return 7.0f / 12.0f; }
bool AudioHandler::has_tone() const { return true; }
