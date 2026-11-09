/* Evaluates Lua declarations into a scene. The renderer resolves its bindings
 * without re-running Lua each frame. */
#pragma once

#include <array>
#include <string>

#include "def_types.hpp"

class AudioHandler;

class Dsl {
public:
    /* The audio handler must outlive this DSL instance. */
    explicit Dsl(const AudioHandler &audio) : audio_(audio) {}

    /* Evaluate the script in a fresh interpreter. Replace the scene only on success. */
    bool run_script(const std::string &script_path);

    /* Resolve a constant or audio source. Audio values are normalised; consumers
     * apply property-specific scaling. */
    float property_value(const Property &property) const;

    /* Resolve an NDC coordinate: constants are literal, tone height retains [0, 1]
     * and other sources span [-1, 1]. */
    float position_value(const Property &property) const;

    /* Resolve HSV components for smoothing before RGB conversion. */
    std::array<float, 3> colour_hsv(const Colour &colour) const;

    const Scene &scene() const { return scene_; }

    /* Mutable access for Lua constructors; the renderer uses the const scene. */
    Scene &mutable_scene() { return scene_; }

private:
    /* Lua constructors recover this instance from the interpreter's extra space. */
    Scene scene_;
    const AudioHandler &audio_;
};
