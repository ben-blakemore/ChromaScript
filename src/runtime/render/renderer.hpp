/* Own the window and GPU resources, reload edited scripts and render each frame.
 * Window operations and the render loop run on the main thread. */
#pragma once

#include <array>
#include <string>
#include <vector>

#include "def_types.hpp"

struct GLFWwindow;
class Dsl;

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    /* Owns a window and GPU handles, so copying would double-free them. */
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    /* Create the window, the GL context and the GPU resources. Returns false having
     * reported the reason, in which case the object stays unused. */
    bool init();

    /* Run until the window is closed, reloading 'script' when it changes on disk and
     * drawing the scene that 'dsl' holds. */
    void run(Dsl &dsl, const std::string &script_path);

private:
    bool create_window();
    void create_gpu_resources();
    void draw_scene(const Dsl &dsl, float frame_duration_seconds);
    void poll_for_reload(Dsl &dsl, const std::string &script_path, double current_time);

    GLFWwindow  *window_ = nullptr;
    unsigned int vertex_array_ = 0, vertex_buffer_ = 0;
    unsigned int shader_program_ = 0;
    int          colour_uniform_location_ = -1;   /* uniform location of uColour */
    float        aspect_ratio_ = 1.0f;     /* width/height, to keep shapes unstretched */

    /* Scratch vertex buffer, reused every frame rather than reallocated. */
    std::vector<float> vertices_;

    /* Smoothed radius per shape; resized when the scene count changes. */
    std::vector<float> display_radius_;

    /* Smoothed HSV per shape. Keep hue circular until conversion to RGB. */
    std::vector<std::array<float, 3>> display_colour_;

    /* Seed colours from their targets on the first frame rather than fading from
     * black. */
    bool colour_seeded_ = false;

    /* Smoothed position per shape, parallel to the radius and colour state. */
    std::vector<std::array<float, 2>> display_position_;

    /* Seed positions from their targets rather than sliding from the centre. */
    bool position_seeded_ = false;

    /* Script-reload polling state. */
    long   last_script_modification_time_ = 0;
    double next_reload_check_time_ = 0.0;
};
