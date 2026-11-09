#include "doctest/doctest.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include <fstream>
#include <utility>

#include "dsl/dsl.hpp"
#include "dsl/utils.hpp"
#include "runtime/audio/audio_handler.hpp"

namespace {

class Diagnostics {
public:
    Diagnostics()
    {
        diagnostic_stream_ = std::tmpfile();
        if (!diagnostic_stream_) throw std::runtime_error("cannot capture script diagnostics");
        saved_stderr_descriptor_ = dup(STDERR_FILENO);
        if (saved_stderr_descriptor_ < 0 || dup2(fileno(diagnostic_stream_), STDERR_FILENO) < 0) {
            if (saved_stderr_descriptor_ >= 0) close(saved_stderr_descriptor_);
            std::fclose(diagnostic_stream_);
            throw std::runtime_error("cannot redirect script diagnostics");
        }
    }

    ~Diagnostics()
    {
        std::fflush(stderr);
        dup2(saved_stderr_descriptor_, STDERR_FILENO);
        close(saved_stderr_descriptor_);
        std::fclose(diagnostic_stream_);
    }

    std::string message()
    {
        std::fflush(stderr);
        std::rewind(diagnostic_stream_);
        std::string result;
        char buffer[256];
        while (std::fgets(buffer, sizeof buffer, diagnostic_stream_)) result += buffer;
        return result;
    }

    Diagnostics(const Diagnostics &) = delete;
    Diagnostics &operator=(const Diagnostics &) = delete;

private:
    FILE *diagnostic_stream_ = nullptr;
    int saved_stderr_descriptor_ = -1;
};

/* Each test gets an isolated script and scene. No display or sound server is
 * needed. Capture expected diagnostics so a successful run stays readable. */
class Fixture {
public:
    Fixture() : dsl(audio)
    {
        char script_name_template[] = "/tmp/chromascript-unit-XXXXXX";
        const int script_descriptor = mkstemp(script_name_template);
        if (script_descriptor < 0) throw std::runtime_error("cannot create temporary script");
        close(script_descriptor);
        script_path = script_name_template;
    }

    ~Fixture() { std::remove(script_path.c_str()); }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;

    bool load(const std::string &source)
    {
        {
            std::ofstream script(script_path);
            script << source;
            if (!script) throw std::runtime_error("cannot write temporary script");
        }
        Diagnostics diagnostics;
        const bool script_loaded = dsl.run_script(script_path);
        diagnostic = diagnostics.message();
        return script_loaded;
    }

    const Shape &shape() const
    {
        REQUIRE(dsl.scene().count() == 1);
        return dsl.scene().shapes.front();
    }

    AudioHandler audio;
    Dsl dsl;
    std::string script_path;
    std::string diagnostic;
};

} // namespace

TEST_CASE("ISCM defaults")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle{}"));
    const Shape &shape = fixture.shape();
    REQUIRE(shape.kind == ShapeKind::Circle);
    REQUIRE(shape.horizontal_position.kind == SourceKind::Fixed);
    CHECK(std::fabs((shape.horizontal_position.fixed) - (0.0f)) < 1e-5f);
    REQUIRE(shape.vertical_position.kind == SourceKind::ToneHeight);
    REQUIRE(shape.radius.kind == SourceKind::Loudness);
    REQUIRE(shape.colour.hue.kind == SourceKind::MainTone);
    REQUIRE(shape.colour.saturation.kind == SourceKind::ToneHeight);
    REQUIRE(shape.colour.value.kind == SourceKind::Loudness);
}

TEST_CASE("all six primitives retain declaration order")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle{}; triangle{}; square{}; pentagon{}; hexagon{}; octagon{}"));
    const ShapeKind expected[] = {ShapeKind::Circle, ShapeKind::Triangle,
        ShapeKind::Square, ShapeKind::Pentagon, ShapeKind::Hexagon, ShapeKind::Octagon};
    REQUIRE(fixture.dsl.scene().count() == 6);
    for (int shape_index = 0; shape_index < 6; ++shape_index) REQUIRE(fixture.dsl.scene().shapes[shape_index].kind == expected[shape_index]);
}

TEST_CASE("constants replace bindings and preserve signed coordinates")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle{x=-0.75, y=0.25, radius=0.3, "
                   "colour={hue=0.2, saturation=0.5, value=0.8}}"));
    const Shape &shape = fixture.shape();
    for (const Property &property : {shape.horizontal_position, shape.vertical_position, shape.radius, shape.colour.hue,
                              shape.colour.saturation, shape.colour.value}) {
        REQUIRE(property.kind == SourceKind::Fixed);
    }
    CHECK(std::fabs((fixture.dsl.position_value(shape.horizontal_position)) - (-0.75f)) < 1e-5f);
    CHECK(std::fabs((fixture.dsl.position_value(shape.vertical_position)) - (0.25f)) < 1e-5f);
    CHECK(std::fabs((fixture.dsl.property_value(shape.radius)) - (0.3f)) < 1e-5f);
    const auto hsv = fixture.dsl.colour_hsv(shape.colour);
    CHECK(std::fabs((hsv[0]) - (0.2f)) < 1e-5f);
    CHECK(std::fabs((hsv[1]) - (0.5f)) < 1e-5f);
    CHECK(std::fabs((hsv[2]) - (0.8f)) < 1e-5f);
}

TEST_CASE("partial colour overrides retain other defaults")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle{colour={value=0.25}}"));
    REQUIRE(fixture.shape().colour.hue.kind == SourceKind::MainTone);
    REQUIRE(fixture.shape().colour.saturation.kind == SourceKind::ToneHeight);
    REQUIRE(fixture.shape().colour.value.kind == SourceKind::Fixed);
    CHECK(std::fabs((fixture.shape().colour.value.fixed) - (0.25f)) < 1e-5f);
}

TEST_CASE("audio bindings resolve to deterministic values")
{
    for (const auto &source : {std::pair<const char *, float>{"bass.amplitude", 0.1f},
                              {"mid.amplitude", 0.2f}, {"treble.amplitude", 0.3f},
                              {"loudness", 0.4f}, {"tone.height", 0.6f},
                              {"tone.main", 7.0f / 12.0f}}) {
        SUBCASE((std::string("resolve binding ") + source.first).c_str()) {
            Fixture fixture;
            REQUIRE(fixture.load(std::string("circle{radius=") + source.first + "}"));
            REQUIRE(fixture.shape().radius.kind != SourceKind::Fixed);
            CHECK(std::fabs((fixture.dsl.property_value(fixture.shape().radius)) - (source.second)) < 1e-5f);
        }
    }
}

TEST_CASE("coordinate sources use their documented ranges")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle{x=bass.amplitude, y=tone.height}"));
    CHECK(std::fabs((fixture.dsl.position_value(fixture.shape().horizontal_position)) - (-0.8f)) < 1e-5f);
    CHECK(std::fabs((fixture.dsl.position_value(fixture.shape().vertical_position)) - (0.6f)) < 1e-5f);
}

TEST_CASE("non-negative radii are accepted")
{
    for (const char *radius : {"0", "0.001", "0.3", "1"}) {
        SUBCASE((std::string("accept non-negative radius ") + radius).c_str()) {
            Fixture fixture;
            REQUIRE(fixture.load(std::string("circle{radius=") + radius + "}"));
            REQUIRE(fixture.shape().radius.fixed >= 0.0f);
        }
    }
}

TEST_CASE("invalid radii are rejected")
{
    for (const char *radius : {"-0.001", "-1", "1/0", "-1/0", "0/0", "1e300"}) {
        SUBCASE((std::string("reject invalid radius ") + radius).c_str()) {
            Fixture fixture;
            REQUIRE(!fixture.load(std::string("circle{radius=") + radius + "}"));
            REQUIRE(fixture.dsl.scene().count() == 0);
            REQUIRE(fixture.diagnostic.find("radius") != std::string::npos);
        }
    }
}

TEST_CASE("non-finite properties are rejected")
{
    for (const char *field : {"x", "y", "hue", "saturation", "value"}) {
        for (const char *value : {"1/0", "-1/0", "0/0", "1e300"}) {
            SUBCASE((std::string("reject non-finite ") + field + "=" + value).c_str()) {
                Fixture fixture;
                const std::string property = std::string(field) + "=" + value;
                const bool coordinate = std::string(field) == "x" || std::string(field) == "y";
                REQUIRE(!fixture.load("circle{" + (coordinate ? property : "colour={" + property + "}") + "}"));
                REQUIRE(fixture.dsl.scene().count() == 0);
                REQUIRE(fixture.diagnostic.find(field) != std::string::npos);
            }
        }
    }
}

TEST_CASE("malformed radii are rejected")
{
    for (const char *value : {"'typo'", "false", "{}", "function() end"}) {
        SUBCASE((std::string("reject malformed radius ") + value).c_str()) {
            Fixture fixture;
            REQUIRE(!fixture.load(std::string("circle{radius=") + value + "}"));
            REQUIRE(fixture.diagnostic.find("radius") != std::string::npos);
        }
    }
}

TEST_CASE("invalid first script leaves no scene")
{
    Fixture fixture;
    REQUIRE(!fixture.load("circle{"));
    REQUIRE(fixture.dsl.scene().count() == 0);
    REQUIRE(!fixture.diagnostic.empty());
}

TEST_CASE("failed reloads preserve the valid scene")
{
    for (const char *source : {"circle{", "circle{radius=0.8}; error('failure')",
                               "circle{radius=-1}"}) {
        SUBCASE((std::string("failed reload preserves valid scene: ") + source).c_str()) {
            Fixture fixture;
            REQUIRE(fixture.load("square{colour='red', x=-0.5, radius=0.3}"));
            REQUIRE(!fixture.load(source));
            const Shape &shape = fixture.shape();
            REQUIRE(shape.kind == ShapeKind::Square);
            CHECK(std::fabs((shape.radius.fixed) - (0.3f)) < 1e-5f);
            CHECK(std::fabs((shape.horizontal_position.fixed) - (-0.5f)) < 1e-5f);
            REQUIRE(shape.colour.hue.kind == SourceKind::Fixed);
            CHECK(std::fabs((shape.colour.hue.fixed) - (0.0f)) < 1e-5f);
        }
    }
}

TEST_CASE("successful reload replaces scene and uses a fresh interpreter")
{
    Fixture fixture;
    REQUIRE(fixture.load("old_global=1; circle{}; triangle{}"));
    REQUIRE(fixture.load("assert(old_global == nil); square{radius=0.2}"));
    REQUIRE(fixture.shape().kind == ShapeKind::Square);
    CHECK(std::fabs((fixture.shape().radius.fixed) - (0.2f)) < 1e-5f);
}

TEST_CASE("caught constructor errors do not append partial shapes")
{
    Fixture fixture;
    REQUIRE(fixture.load("local ok=pcall(function() circle{radius=-1} end); "
                   "assert(not ok); square{radius=0.2}"));
    REQUIRE(fixture.shape().kind == ShapeKind::Square);
}

TEST_CASE("reentrant field callbacks cannot invalidate shapes")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle(setmetatable({}, {__index=function(_, key) "
                   "if key=='colour' then for i=1,100 do square{radius=0.2} end "
                   "return 'red' end end}))"));
    REQUIRE(fixture.dsl.scene().count() == 101);
    for (int shape_index = 0; shape_index < 100; ++shape_index) {
        const Shape &shape = fixture.dsl.scene().shapes[shape_index];
        REQUIRE(shape.kind == ShapeKind::Square);
        CHECK(std::fabs((shape.radius.fixed) - (0.2f)) < 1e-5f);
    }
    const Shape &outer = fixture.dsl.scene().shapes.back();
    REQUIRE(outer.kind == ShapeKind::Circle);
    REQUIRE(outer.colour.hue.kind == SourceKind::Fixed);
    CHECK(std::fabs((outer.colour.hue.fixed) - (0.0f)) < 1e-5f);
}

TEST_CASE("caught callback errors leave no partial shape")
{
    Fixture fixture;
    REQUIRE(fixture.load("local ok=pcall(function() circle{colour=setmetatable({}, "
                   "{__index=function() error('field failure') end})} end); "
                   "assert(not ok); square{}"));
    REQUIRE(fixture.shape().kind == ShapeKind::Square);
}

TEST_CASE("Lua finalisers cannot change the committed scene")
{
    Fixture fixture;
    REQUIRE(fixture.load("local t=setmetatable({}, {__gc=function() triangle{} end}); "
                   "circle{radius=0.2}"));
    REQUIRE(fixture.shape().kind == ShapeKind::Circle);
}

TEST_CASE("invalid declarations are rejected")
{
    for (const char *source : {"circle()", "circle(5)", "circle({}, {})",
                               "circle{colour='unknown'}", "circle{colour=0/0}",
                               "circle{colour=false}", "error({})"}) {
        SUBCASE((std::string("reject invalid declaration: ") + source).c_str()) {
            Fixture fixture;
            REQUIRE(!fixture.load(source));
            REQUIRE(fixture.dsl.scene().count() == 0);
            REQUIRE(!fixture.diagnostic.empty());
        }
    }
}

TEST_CASE("a bare colour source binds hue")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle{colour=bass.amplitude}"));
    REQUIRE(fixture.shape().colour.hue.kind == SourceKind::BandAmplitude);
    CHECK(std::fabs((fixture.dsl.colour_hsv(fixture.shape().colour)[0]) - (0.1f)) < 1e-5f);
}

TEST_CASE("named red sets an opaque vivid colour")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle{colour='red'}"));
    const auto hsv = fixture.dsl.colour_hsv(fixture.shape().colour);
    CHECK(std::fabs((hsv[0]) - (0.0f)) < 1e-5f);
    CHECK(std::fabs((hsv[1]) - (1.0f)) < 1e-5f);
    CHECK(std::fabs((hsv[2]) - (1.0f)) < 1e-5f);
}

TEST_CASE("named colours resolve through Lua")
{
    struct NamedCase { const char *name; std::array<float, 3> rgb; };
    for (const NamedCase colour_case : {
            NamedCase{"green", {0.0f, 1.0f, 0.0f}},
            {"orange", {1.0f, 0.5f, 0.0f}}, {"yellow", {1.0f, 1.0f, 0.0f}},
            {"white", {1.0f, 1.0f, 1.0f}}, {"grey", {0.5f, 0.5f, 0.5f}},
            {"black", {0.0f, 0.0f, 0.0f}}}) {
        SUBCASE((std::string("Lua named colour: ") + colour_case.name).c_str()) {
            Fixture fixture;
            REQUIRE(fixture.load(std::string("circle{colour='") + colour_case.name + "'}"));
            const Colour &colour = fixture.shape().colour;
            REQUIRE(colour.hue.kind == SourceKind::Fixed);
            REQUIRE(colour.saturation.kind == SourceKind::Fixed);
            REQUIRE(colour.value.kind == SourceKind::Fixed);
            const auto hsv = fixture.dsl.colour_hsv(colour);
            const auto rgb = hsv_to_rgb(hsv[0], hsv[1], hsv[2]);
            for (int channel_index = 0; channel_index < 3; ++channel_index) CHECK(std::fabs((rgb[channel_index]) - (colour_case.rgb[channel_index])) < 1e-5f);
        }
    }
}

TEST_CASE("main-tone hue matches its named colour")
{
    Fixture fixture;
    /* The audio stub reports G, whose Marcotone name is green-blue. */
    REQUIRE(fixture.load("circle{colour={hue=tone.main, saturation=1, value=1}}; "
                   "circle{colour='green-blue'}"));
    REQUIRE(fixture.dsl.scene().count() == 2);
    const auto bound = fixture.dsl.colour_hsv(fixture.dsl.scene().shapes[0].colour);
    const auto named = fixture.dsl.colour_hsv(fixture.dsl.scene().shapes[1].colour);
    for (int channel_index = 0; channel_index < 3; ++channel_index) CHECK(std::fabs((bound[channel_index]) - (named[channel_index])) < 1e-5f);
    CHECK(std::fabs((bound[0]) - (0.5f)) < 1e-5f);
}

TEST_CASE("a missing script preserves the existing scene")
{
    Fixture fixture;
    REQUIRE(fixture.load("circle{radius=0.2}"));
    std::remove(fixture.script_path.c_str());
    bool loaded;
    {
        Diagnostics diagnostics;
        loaded = fixture.dsl.run_script(fixture.script_path);
    }
    REQUIRE(!loaded);
    CHECK(std::fabs((fixture.shape().radius.fixed) - (0.2f)) < 1e-5f);
}

TEST_CASE("example scripts load successfully")
{
    for (const char *name : {"circle", "shapes", "colour", "position", "brightness",
                            "saturation", "mandala", "mandala_single_bloom"}) {
        SUBCASE((std::string("example script: ") + name).c_str()) {
            Fixture fixture;
            std::ifstream script(std::string("scripts/") + name + ".lua");
            REQUIRE(script.good());
            const std::string source((std::istreambuf_iterator<char>(script)),
                                      std::istreambuf_iterator<char>());
            REQUIRE(fixture.load(source));
            REQUIRE(fixture.dsl.scene().count() > 0);
        }
    }
}

