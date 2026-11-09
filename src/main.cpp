/* Load a Lua scene, capture and analyse audio on a worker thread and render on the
 * main thread. GLFW window operations stay on the main thread. */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "dsl/dsl.hpp"
#include "runtime/audio/audio_handler.hpp"
#include "runtime/render/renderer.hpp"

int main(int argument_count, char **arguments)
{
    if (argument_count < 2) {
        std::fprintf(stderr, "Must pass user script to run against\n");
        return EXIT_FAILURE;
    }

    const std::string script_path = arguments[1];

    /* Capture starts on construction and stops when the handler is destroyed. */
    AudioHandler audio;

    Dsl dsl(audio);

    /* Validate the initial scene before opening a window. */
    if (!dsl.run_script(script_path)) {
        return EXIT_FAILURE;
    }
    if (dsl.scene().count() == 0) {
        std::fprintf(stderr, "script produced no shapes; nothing to draw\n");
        return EXIT_FAILURE;
    }

    Renderer renderer;
    if (!renderer.init()) {
        return EXIT_FAILURE;
    }

    renderer.run(dsl, script_path);

    return EXIT_SUCCESS;
}
