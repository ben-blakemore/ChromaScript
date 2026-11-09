#include "dsl/dsl.hpp"

#include <lua.hpp>

#include <cstdio>
#include <cstring>
#include <cmath>

#include "dsl/utils.hpp"
#include "runtime/audio/audio_handler.hpp"

/* Resolve the current source value, leaving property-specific scaling to the
 * consumer. Fixed values pass through unchanged. */
float Dsl::property_value(const Property &property) const
{
    switch (property.kind) {
        case SourceKind::BandAmplitude: return audio_.band(property.band);
        case SourceKind::MainTone:      return audio_.main_tone();
        case SourceKind::ToneHeight:    return audio_.tone_height();
        case SourceKind::Loudness:      return audio_.loudness();
        case SourceKind::Fixed:         break;
    }
    return property.fixed;
}

/* Keep fixed coordinates literal and tone height in [0, 1]. */
float Dsl::position_value(const Property &property) const
{
    if (property.kind == SourceKind::Fixed) {
        return property.fixed;
    }
    if (property.kind == SourceKind::ToneHeight) {
        return property_value(property);
    }
    return property_value(property) * 2.0f - 1.0f;
}

/* Return HSV so the renderer can smooth hue around the colour wheel before
 * converting to RGB. */
std::array<float, 3> Dsl::colour_hsv(const Colour &colour) const
{
    /* MainTone is normalised by pitch class for general property bindings. When it
     * drives hue, use the same explicit palette as named colours. */
    const float source_hue = property_value(colour.hue);
    const float hue = colour.hue.kind == SourceKind::MainTone
        ? hue_from_pitch_class(static_cast<int>(std::lround(source_hue * 12.0f)))
        : source_hue;
    /* Keep main-tone colours grey until a pitch class is selected. Explicit hues are
     * unaffected. */
    if (colour.hue.kind == SourceKind::MainTone && !audio_.has_tone()) {
        return { hue, 0.0f, property_value(colour.value) };
    }

    return { hue,
             property_value(colour.saturation),
             property_value(colour.value) };
}

namespace {

/* Recover the owning DSL from Lua's per-state extra space. */
Dsl *owner_of(lua_State *lua_state)
{
    Dsl *owner = nullptr;
    std::memcpy(&owner, lua_getextraspace(lua_state), sizeof owner);
    return owner;
}

/* Append one shape per constructor call. Locals must not own resources: Lua errors
 * use longjmp and skip destructors. */
int make_shape(lua_State *lua_state, ShapeKind kind)
{
    /* Each constructor takes exactly one property table. */
    luaL_checktype(lua_state, 1, LUA_TTABLE);
    if (lua_gettop(lua_state) != 1) {
        return luaL_error(lua_state, "shape expects a single property table");
    }

    Scene &scene = owner_of(lua_state)->mutable_scene();

    /* Parse locally before appending: Lua callbacks can reallocate the scene, and
     * caught errors must leave no partial shape. */
    Shape shape;
    shape.kind = kind;

    /* Seed ISCM colour bindings; supplied fields override components independently
     * (REQ-RUN-007; REQ-DSL-002). */
    shape.colour.hue        = Property{ SourceKind::MainTone,   0.0f, BandBass };
    shape.colour.saturation = Property{ SourceKind::ToneHeight, 0.0f, BandBass };
    shape.colour.value      = Property{ SourceKind::Loudness,   0.0f, BandBass };

    /* Read colour from the constructor's argument table at stack index 1. */
    lua_getfield(lua_state, 1, "colour");
    if (lua_type(lua_state, -1) == LUA_TSTRING) {
        /* A name fixes all three components, including the zero saturation needed by
         * white, grey and black. */
        std::array<float, 3> named_colour_hsv;
        if (hsv_from_name(lua_tostring(lua_state, -1), named_colour_hsv)) {
            shape.colour.hue        = Property{ SourceKind::Fixed, named_colour_hsv[0], BandBass };
            shape.colour.saturation = Property{ SourceKind::Fixed, named_colour_hsv[1], BandBass };
            shape.colour.value      = Property{ SourceKind::Fixed, named_colour_hsv[2], BandBass };
        } else {
            /* Existing scripts may give a bare source to drive hue. */
            property_from_lua_value(lua_state, "colour", shape.colour.hue);
        }
    } else if (lua_istable(lua_state, -1)) {
        /* Override supplied HSV components; omitted fields retain their bindings. */
        override_property(lua_state, "hue",        shape.colour.hue);
        override_property(lua_state, "saturation", shape.colour.saturation);
        override_property(lua_state, "value",      shape.colour.value);
    } else if (!lua_isnil(lua_state, -1)) {
        return luaL_error(lua_state, "'colour' must be a colour name, audio source or HSV table");
    }
    lua_pop(lua_state, 1);

    /* The argument table is back on top. Default to centred x, tone-height y and
     * loudness-driven radius; allow individual overrides. */
    read_property(lua_state, "x", 0.0f, shape.horizontal_position);
    shape.vertical_position = Property{ SourceKind::ToneHeight, 0.0f, BandBass };
    override_property(lua_state, "y", shape.vertical_position);
    shape.radius = Property{ SourceKind::Loudness, 0.0f, BandBass };
    override_property(lua_state, "radius", shape.radius);
    if (shape.radius.kind == SourceKind::Fixed && shape.radius.fixed < 0.0f) {
        return luaL_error(lua_state, "'radius' must be non-negative");
    }

    scene.shapes.push_back(shape);

    /* Constructors append shapes without returning Lua values. */
    return 0;
}

/* Lua callbacks accept only a state pointer, so each wrapper supplies its shape
 * kind. */
int l_circle(lua_State *lua_state)   { return make_shape(lua_state, ShapeKind::Circle);   }
int l_triangle(lua_State *lua_state) { return make_shape(lua_state, ShapeKind::Triangle); }
int l_square(lua_State *lua_state)   { return make_shape(lua_state, ShapeKind::Square);   }
int l_pentagon(lua_State *lua_state) { return make_shape(lua_state, ShapeKind::Pentagon); }
int l_hexagon(lua_State *lua_state)  { return make_shape(lua_state, ShapeKind::Hexagon);  }
int l_octagon(lua_State *lua_state)  { return make_shape(lua_state, ShapeKind::Octagon);  }

/* Closes the Lua state on any exit path, including an early return. */
struct LuaStateGuard {
    lua_State *lua_state;
    ~LuaStateGuard() { if (lua_state) lua_close(lua_state); }
};

}  // namespace

/* Create a fresh interpreter, expose constructors and audio markers and evaluate the
 * script into a candidate scene. */
bool Dsl::run_script(const std::string &script_path)
{
    /* The candidate outlives the Lua guard because closing Lua can run constructor
     * callbacks. */
    Dsl candidate_dsl(audio_);
    /* A fresh interpreter prevents globals leaking between revisions. */
    lua_State *lua_state = luaL_newstate();
    if (!lua_state) {
        std::fprintf(stderr, "cannot create Lua state\n");
        return false;
    }
    LuaStateGuard lua_state_guard{lua_state};

    /* Store the candidate owner for constructor callbacks. */
    Dsl *owner = &candidate_dsl;
    std::memcpy(lua_getextraspace(lua_state), &owner, sizeof owner);

    /* Expose the Lua standard libraries for scene construction. */
    luaL_openlibs(lua_state);

    /* Register each shape constructor as a Lua global. */
    lua_register(lua_state, "circle",   l_circle);
    lua_register(lua_state, "triangle", l_triangle);
    lua_register(lua_state, "square",   l_square);
    lua_register(lua_state, "pentagon", l_pentagon);
    lua_register(lua_state, "hexagon",  l_hexagon);
    lua_register(lua_state, "octagon",  l_octagon);

    /* Audio markers record bindings at script load; they do not sample live values. */
    for (int band_index = 0; band_index < BandCount; ++band_index) {
        /* Set the amplitude marker on the band table, then publish it as a global.
         * Both setters pop their values. */
        lua_newtable(lua_state);
        lua_pushstring(lua_state, band_marker(static_cast<Band>(band_index)));
        lua_setfield(lua_state, -2, "amplitude");
        lua_setglobal(lua_state, band_name(static_cast<Band>(band_index)));
    }

    /* Expose the main-tone and tone-height markers on one table. */
    lua_newtable(lua_state);
    lua_pushstring(lua_state, main_tone_marker());
    lua_setfield(lua_state, -2, "main");
    lua_pushstring(lua_state, tone_height_marker());
    lua_setfield(lua_state, -2, "height");
    lua_setglobal(lua_state, "tone");

    /* Expose overall loudness as a single global marker. */
    lua_pushstring(lua_state, loudness_marker());
    lua_setglobal(lua_state, "loudness");

    /* Evaluate once; the resulting bindings remain active until the next reload. */
    if (luaL_dofile(lua_state, script_path.c_str()) != LUA_OK) {
        const char *message = lua_tostring(lua_state, -1);
        std::fprintf(stderr, "script error: %s\n", message ? message : "non-string Lua error");
        return false;
    }

    scene_.shapes.swap(candidate_dsl.scene_.shapes);

    /* The scene now holds the bindings independently of the Lua interpreter. */
    return true;
}
