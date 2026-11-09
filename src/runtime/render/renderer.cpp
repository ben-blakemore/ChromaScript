#include "runtime/render/renderer.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <sys/stat.h>   /* stat(), for detecting file changes */

#include <array>
#include <cmath>
#include <cstdio>

#include "dsl/dsl.hpp"
#include "dsl/utils.hpp"   /* hsv_to_rgb, applied after the colour envelope */

namespace {

constexpr int CircleSegments = 64;

/* Vertices for one shape: a centre point plus a closing ring, two floats each. Sized
 * for the worst case (a circle) so the same buffer serves every kind. */
constexpr int MaxShapeVertexComponents = (CircleSegments + 2) * 2;

/* Radius smoothing in seconds: fast attack preserves transients; slower release
 * softens decay. */
constexpr float RadiusAttackTimeSeconds  = 0.010f;
constexpr float RadiusReleaseTimeSeconds = 0.09f;

/* Map bound radii into a visible range using a nonlinear response. Fixed radii
 * bypass this scaling. */
constexpr float RadiusFloor   = 0.05f;
constexpr float RadiusRange   = 0.60f;
constexpr float RadiusGamma   = 2.2f;

/* Keep bound brightness visible against black. Fixed brightness bypasses this floor. */
constexpr float BrightnessFloor = 0.12f;
constexpr float BrightnessRange = 1.0f - BrightnessFloor;

/* Symmetric hue and saturation smoothing, in seconds. */
constexpr float ColourTimeConstantSeconds = 0.12f;

/* Brightness smoothing in seconds: fast attack preserves transients; slower release
 * reduces flicker. */
constexpr float BrightnessAttackTimeSeconds  = 0.015f;
constexpr float BrightnessReleaseTimeSeconds = 0.18f;

/* Symmetric position smoothing, in seconds, to reduce jitter in both directions. */
constexpr float PositionTimeConstantSeconds = 0.12f;

/* Approach a target with frame-rate-independent exponential smoothing. */
float approach(float current, float target, float time_constant_seconds, float frame_duration_seconds)
{
    return current + (target - current) * (1.0f - std::exp(-frame_duration_seconds / time_constant_seconds));
}

/* Smooth hue along the shortest arc around the colour wheel. */
float approach_hue(float current, float target, float time_constant_seconds, float frame_duration_seconds)
{
    float hue_difference = target - current;
    hue_difference -= std::floor(hue_difference + 0.5f);          /* wrap into [-0.5, 0.5) */
    float smoothed_hue = current + hue_difference
                         * (1.0f - std::exp(-frame_duration_seconds / time_constant_seconds));
    return smoothed_hue - std::floor(smoothed_hue);                   /* keep in [0, 1) */
}

/* How often to check the script file for edits, in seconds. */
constexpr double ReloadPollInterval = 0.25;

const char *VertexShaderSource =
    "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";

const char *FragmentShaderSource =
    "#version 330 core\n"
    "uniform vec3 uColour;\n"
    "out vec4 FragColour;\n"
    "void main() { FragColour = vec4(uColour, 1.0); }\n";

GLuint compile_shader(GLenum shader_type, const char *shader_source)
{
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &shader_source, nullptr);
    glCompileShader(shader);
    GLint compiled_successfully = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled_successfully);
    if (!compiled_successfully) {
        char compilation_log[512];
        glGetShaderInfoLog(shader, sizeof compilation_log, nullptr, compilation_log);
        std::fprintf(stderr, "shader compile error: %s\n", compilation_log);
    }
    return shader;
}

GLuint link_program()
{
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, VertexShaderSource);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, FragmentShaderSource);
    GLuint shader_program  = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return shader_program;
}

/* Regular-polygon side counts and starting angles; orient triangles upwards and
 * even-sided shapes on a level edge. */
struct Geometry { int side_count; float start_angle_radians; };

Geometry geometry_for(ShapeKind kind)
{
    constexpr float HalfPi = static_cast<float>(M_PI) / 2.0f;

    switch (kind) {
        case ShapeKind::Triangle:
            return { 3, HalfPi };
        case ShapeKind::Square:
            return { 4, static_cast<float>(M_PI) / 4.0f };
        case ShapeKind::Pentagon:
            return { 5, HalfPi };
        case ShapeKind::Hexagon:
            return { 6, 0.0f };
        case ShapeKind::Octagon:
            return { 8, static_cast<float>(M_PI) / 8.0f };
        case ShapeKind::Circle:
        default:
            return { CircleSegments, 0.0f };
    }
}

/* Build a triangle fan at the resolved position and radius, correcting x for aspect
 * ratio. */
int build_shape(float *vertices, float aspect_ratio, float centre_x, float centre_y, float radius,
                ShapeKind kind)
{
    const Geometry geometry = geometry_for(kind);
    int component_index = 0;
    vertices[component_index++] = centre_x;     /* centre x */
    vertices[component_index++] = centre_y;     /* centre y */
    for (int side_index = 0; side_index <= geometry.side_count; ++side_index) {
        float angle_radians = geometry.start_angle_radians
                              + static_cast<float>(side_index) / geometry.side_count * 2.0f
                              * static_cast<float>(M_PI);
        vertices[component_index++] = centre_x + std::cos(angle_radians) * radius / aspect_ratio;
        vertices[component_index++] = centre_y + std::sin(angle_radians) * radius;
    }
    return component_index / 2;   /* vertex count */
}

long file_mtime(const std::string &path)
{
    struct stat file_status{};
    if (stat(path.c_str(), &file_status) != 0) {
        return 0;
    }
    return static_cast<long>(file_status.st_mtime);
}

}  // namespace

Renderer::~Renderer()
{
    /* Guarded because init() may have failed before creating any of these. */
    if (shader_program_) glDeleteProgram(shader_program_);
    if (vertex_buffer_)     glDeleteBuffers(1, &vertex_buffer_);
    if (vertex_array_)     glDeleteVertexArrays(1, &vertex_array_);
    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

/* Create an OpenGL 3.3 core context before loading GL function pointers. */
bool Renderer::create_window()
{
    if (!glfwInit()) {
        std::fprintf(stderr, "failed to initialise GLFW\n");
        return false;
    }

    /* Request 3.3 core, which is what the shaders declare (#version 330 core). */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    /* Match the desktop video mode for full-screen output without changing the
     * monitor's resolution or refresh rate. */
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode) {
        std::fprintf(stderr, "failed to get the primary monitor's video mode\n");
        glfwTerminate();
        return false;
    }
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    /* Keep the visualisation visible while another monitor has input focus. */
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

    window_ = glfwCreateWindow(mode->width, mode->height, "DSL", monitor, nullptr);
    if (!window_) {
        std::fprintf(stderr, "failed to create window\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);   /* Vsync prevents tearing. */

    /* GL function loading requires the context made current above. */
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::fprintf(stderr, "failed to load OpenGL functions\n");
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        return false;
    }

    return true;
}

/* Allocate a reusable vertex buffer for the largest shape and compile the shaders. */
void Renderer::create_gpu_resources()
{
    vertices_.resize(MaxShapeVertexComponents);

    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);
    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices_.size() * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);

    /* Attribute 0 is vec2 aPos in the vertex shader: two floats, tightly packed. */
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    shader_program_    = link_program();
    colour_uniform_location_ = glGetUniformLocation(shader_program_, "uColour");
}

bool Renderer::init()
{
    if (!create_window()) {
        return false;
    }
    create_gpu_resources();
    return true;
}

/* Resolve mappings, smooth display values and upload each shape's geometry and
 * colour. */
void Renderer::draw_scene(const Dsl &dsl, float frame_duration_seconds)
{
    /* Framebuffer pixels can differ from window coordinates on scaled displays, and
     * can change when the window moves between monitors. */
    int framebuffer_width = 0, framebuffer_height = 0;
    glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) {
        return;
    }
    glViewport(0, 0, framebuffer_width, framebuffer_height);
    aspect_ratio_ = static_cast<float>(framebuffer_width) / static_cast<float>(framebuffer_height);

    const Scene &scene = dsl.scene();

    /* Match radius state to the scene; new entries start at zero. */
    if (display_radius_.size() != scene.count()) {
        display_radius_.resize(scene.count(), 0.0f);
    }

    /* New colour entries are seeded from their resolved values. */
    if (display_colour_.size() != scene.count()) {
        display_colour_.resize(scene.count());
        colour_seeded_ = false;
    }

    /* New position entries are seeded in place rather than sliding from the centre. */
    if (display_position_.size() != scene.count()) {
        display_position_.resize(scene.count());
        position_seeded_ = false;
    }

    for (std::size_t shape_index = 0; shape_index < scene.count(); ++shape_index) {
        const Shape &shape = scene.shapes[shape_index];

        /* Scale reactive radii; fixed radii keep their literal values. */
        float radius = dsl.property_value(shape.radius);
        if (shape.radius.kind != SourceKind::Fixed) {
            radius = RadiusFloor
                     + RadiusRange * std::pow(radius, RadiusGamma);
        }

        if (shape.radius.kind != SourceKind::Fixed) {
            // One-pole envelope with asymmetric time constants
            float time_constant_seconds = (radius > display_radius_[shape_index]) ? RadiusAttackTimeSeconds
                                                      : RadiusReleaseTimeSeconds;
            display_radius_[shape_index] = approach(display_radius_[shape_index], radius,
                                                   time_constant_seconds, frame_duration_seconds);
            radius = display_radius_[shape_index];
        }
        /* Resolve coordinates from the latest audio values. */
        float target_x = dsl.position_value(shape.horizontal_position);
        float target_y = dsl.position_value(shape.vertical_position);

        /* Smooth each bound axis independently; fixed coordinates remain literal. */
        std::array<float, 2> &displayed_position = display_position_[shape_index];

        if (!position_seeded_) {
            displayed_position = { target_x, target_y };   /* first frame: appear in place */
        } else {
            if (shape.horizontal_position.kind != SourceKind::Fixed) {
                displayed_position[0] = approach(displayed_position[0], target_x,
                                                 PositionTimeConstantSeconds, frame_duration_seconds);
            } else {
                displayed_position[0] = target_x;
            }
            if (shape.vertical_position.kind != SourceKind::Fixed) {
                displayed_position[1] = approach(displayed_position[1], target_y,
                                                 PositionTimeConstantSeconds, frame_duration_seconds);
            } else {
                displayed_position[1] = target_y;
            }
        }

        const int vertex_count = build_shape(vertices_.data(), aspect_ratio_,
                                           displayed_position[0], displayed_position[1],
                                           radius, shape.kind);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(vertex_count * 2 * sizeof(float)),
                        vertices_.data());
        /* Resolve the current HSV targets before smoothing. */
        std::array<float, 3> target_colour = dsl.colour_hsv(shape.colour);

        /* Apply the brightness floor before smoothing, and only to audio bindings. */
        if (shape.colour.value.kind != SourceKind::Fixed) {
            target_colour[2] = BrightnessFloor + BrightnessRange * target_colour[2];
        }

        std::array<float, 3> &displayed_colour = display_colour_[shape_index];

        if (!colour_seeded_) {
            displayed_colour = target_colour;   /* first frame: appear in colour, do not fade in */
        } else {
            displayed_colour[0] = approach_hue(displayed_colour[0], target_colour[0],
                                               ColourTimeConstantSeconds, frame_duration_seconds);
            displayed_colour[1] = approach(displayed_colour[1], target_colour[1],
                                           ColourTimeConstantSeconds, frame_duration_seconds);

            /* Brightness rises quickly and falls slowly, so a transient registers
             * rather than being averaged away; see the constants. */
            const float brightness_time_constant_seconds =
                (target_colour[2] > displayed_colour[2]) ? BrightnessAttackTimeSeconds
                                                       : BrightnessReleaseTimeSeconds;
            displayed_colour[2] = approach(displayed_colour[2], target_colour[2],
                                           brightness_time_constant_seconds, frame_duration_seconds);
        }

        const std::array<float, 3> rgb_colour =
            hsv_to_rgb(displayed_colour[0], displayed_colour[1], displayed_colour[2]);
        glUniform3fv(colour_uniform_location_, 1, rgb_colour.data());
        glDrawArrays(GL_TRIANGLE_FAN, 0, vertex_count);
    }

    /* Finish seeding all shapes before subsequent frames begin smoothing. */
    colour_seeded_ = true;
    position_seeded_    = true;
}

/* Reload edited scripts at the configured polling interval. */
void Renderer::poll_for_reload(Dsl &dsl, const std::string &script_path, double current_time)
{
    if (current_time < next_reload_check_time_) {
        return;
    }
    /* Avoid a filesystem check on every frame. */
    next_reload_check_time_ = current_time + ReloadPollInterval;

    const long modification_time = file_mtime(script_path);
    if (modification_time == 0 || modification_time == last_script_modification_time_) {
        return;
    }
    last_script_modification_time_ = modification_time;
    std::printf("script changed, reloading\n");

    /* Failed evaluations report an error and retain the last valid scene
     * (REQ-RUN-012). A replacement starts with its own display state. */
    if (dsl.run_script(script_path)) {
        display_radius_.clear();
        display_colour_.clear();
        display_position_.clear();
        colour_seeded_ = false;
        position_seeded_ = false;
    }
}

void Renderer::run(Dsl &dsl, const std::string &script_path)
{
    last_script_modification_time_ = file_mtime(script_path);
    next_reload_check_time_ = 0.0;

    double previous_frame_time = glfwGetTime();

    /* Bound once rather than per frame: nothing here ever binds anything else, so
     * the state stays valid for the lifetime of the loop. */
    glUseProgram(shader_program_);
    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);

    while (!glfwWindowShouldClose(window_)) {
        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window_, 1);
        }

        const double current_time = glfwGetTime();
        const float  frame_duration_seconds  = static_cast<float>(current_time - previous_frame_time);
        previous_frame_time = current_time;

        poll_for_reload(dsl, script_path, current_time);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        draw_scene(dsl, frame_duration_seconds);

        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}
