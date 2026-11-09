#include "dsl/utils.hpp"

#include <cmath>
#include <cstring>
#include <limits>

/* Audio sources are Lua marker strings recognised when parsing properties. Numbers
 * become constants. Literal marker strings can also be interpreted as bindings. */
namespace {

constexpr const char *BassMarker   = "__bass_amplitude__";
constexpr const char *MidMarker    = "__mid_amplitude__";
constexpr const char *TrebleMarker = "__treble_amplitude__";

/* Expose the two tone readings as fields on the tone table. */
constexpr const char *MainToneMarker   = "__tone_main__";
constexpr const char *ToneHeightMarker = "__tone_height__";

/* Expose overall loudness as a single global marker. */
constexpr const char *LoudnessMarker = "__loudness__";

struct NamedColour { const char *name; float hue_degrees, saturation, value; };

/* The first twelve entries preserve Maryon's pitch-class associations. HSV values
 * are display approximations of the named colours. */
constexpr NamedColour Palette[] = {
    { "red",             0.0f, 1.0f, 1.0f },
    { "red-orange",     15.0f, 1.0f, 1.0f },
    { "orange",         30.0f, 1.0f, 1.0f },
    { "orange-yellow",  45.0f, 1.0f, 1.0f },
    { "yellow",         60.0f, 1.0f, 1.0f },
    { "yellow-green",   90.0f, 1.0f, 1.0f },
    { "green",         120.0f, 1.0f, 1.0f },
    { "green-blue",    180.0f, 1.0f, 1.0f },
    { "blue",          240.0f, 1.0f, 1.0f },
    { "blue-violet",   255.0f, 1.0f, 1.0f },
    { "violet",        270.0f, 1.0f, 1.0f },
    { "violet-red",    330.0f, 1.0f, 1.0f },
    { "cyan",          180.0f, 1.0f, 1.0f },
    { "magenta",       300.0f, 1.0f, 1.0f },
    { "white",           0.0f, 0.0f, 1.0f },
    { "grey",            0.0f, 0.0f, 0.5f },
    { "black",           0.0f, 0.0f, 0.0f },
};

}  // namespace

const char *main_tone_marker()   { return MainToneMarker; }
const char *tone_height_marker() { return ToneHeightMarker; }
const char *loudness_marker()    { return LoudnessMarker; }

const char *band_marker(Band band)
{
    switch (band) {
        case BandBass:   return BassMarker;
        case BandMid:    return MidMarker;
        case BandTreble: return TrebleMarker;
        default:         return nullptr;
    }
}

const char *band_name(Band band)
{
    switch (band) {
        case BandBass:   return "bass";
        case BandMid:    return "mid";
        case BandTreble: return "treble";
        default:         return nullptr;
    }
}

bool hsv_from_name(const char *name, std::array<float, 3> &hsv_output)
{
    for (const auto &named_colour : Palette) {
        if (std::strcmp(name, named_colour.name) == 0) {
            hsv_output = { named_colour.hue_degrees / 360.0f, named_colour.saturation, named_colour.value };
            return true;
        }
    }
    return false;
}

bool hue_from_name(const char *name, float &hue_output)
{
    std::array<float, 3> hsv;
    if (!hsv_from_name(name, hsv)) return false;
    hue_output = hsv[0];
    return true;
}

/* Look up the Marcotone hue, wrapping pitch-class indices into [0, 11]. */
float hue_from_pitch_class(int pitch_class)
{
    const int palette_index = ((pitch_class % 12) + 12) % 12;   /* handles negatives too */
    return Palette[palette_index].hue_degrees / 360.0f;
}

/* Convert HSV to RGB by colour-wheel sector. Hue wraps; saturation and brightness
 * clamp to [0, 1]. */
std::array<float, 3> hsv_to_rgb(float hue, float saturation, float brightness)
{
    /* Wrap hue and clamp saturation and brightness. */
    hue -= std::floor(hue);
    saturation = saturation < 0.0f ? 0.0f : (saturation > 1.0f ? 1.0f : saturation);
    brightness = brightness < 0.0f ? 0.0f : (brightness > 1.0f ? 1.0f : brightness);

    const float sector = hue * 6.0f;
    const int   sector_index      = static_cast<int>(sector);
    const float sector_fraction      = sector - static_cast<float>(sector_index);

    const float minimum_channel = brightness * (1.0f - saturation);
    const float descending_channel = brightness * (1.0f - saturation * sector_fraction);
    const float ascending_channel = brightness * (1.0f - saturation * (1.0f - sector_fraction));

    switch (sector_index) {
        case 0:  return { brightness, ascending_channel, minimum_channel };
        case 1:  return { descending_channel, brightness, minimum_channel };
        case 2:  return { minimum_channel, brightness, ascending_channel };
        case 3:  return { minimum_channel, descending_channel, brightness };
        case 4:  return { ascending_channel, minimum_channel, brightness };
        default: return { brightness, minimum_channel, descending_channel };
                 /* Final sector, including rounding to sector 6. */
    }
}

/* Start with a fixed default, then read an override from the table on top of the
 * stack. */
void read_property(lua_State *lua_state, const char *field_name, float default_value, Property &property)
{
    /* Only a missing field keeps the default; malformed input is an error. */
    property.kind  = SourceKind::Fixed;
    property.fixed = default_value;
    override_property(lua_state, field_name, property);
}

/* Preserve the existing property when its field is absent. */
void override_property(lua_State *lua_state, const char *field_name, Property &property)
{
    /* Push the field value; the matching pop restores the table to the top. */
    lua_getfield(lua_state, -1, field_name);

    property_from_lua_value(lua_state, field_name, property);
    lua_pop(lua_state, 1);
}

void property_from_lua_value(lua_State *lua_state, const char *field_name, Property &property)
{
    /* Check the exact type: lua_isstring also accepts numbers. */
    if (lua_type(lua_state, -1) == LUA_TSTRING) {
        /* Lua owns the marker pointer; compare it immediately without storing it. */
        const char *source_marker = lua_tostring(lua_state, -1);
        for (int band_index = 0; band_index < BandCount; ++band_index) {
            if (std::strcmp(source_marker, band_marker(static_cast<Band>(band_index))) == 0) {
                property.kind = SourceKind::BandAmplitude;
                property.band = static_cast<Band>(band_index);
                return;
            }
        }
        if (std::strcmp(source_marker, main_tone_marker()) == 0) {
            property.kind = SourceKind::MainTone;
        } else if (std::strcmp(source_marker, tone_height_marker()) == 0) {
            property.kind = SourceKind::ToneHeight;
        } else if (std::strcmp(source_marker, loudness_marker()) == 0) {
            property.kind = SourceKind::Loudness;
        } else {
            luaL_error(lua_state, "'%s' must be a finite number or an audio source", field_name);
        }
    } else if (lua_type(lua_state, -1) == LUA_TNUMBER) {
        const lua_Number value = lua_tonumber(lua_state, -1);
        /* Check before conversion: a finite Lua double may overflow a float. */
        if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max()) {
            luaL_error(lua_state, "'%s' must be a finite number representable as a float", field_name);
        }
        /* Set both the tag and value so a constant replaces any previous binding. */
        property.kind  = SourceKind::Fixed;
        property.fixed = static_cast<float>(value);
    } else if (!lua_isnil(lua_state, -1)) {
        luaL_error(lua_state, "'%s' must be a finite number or an audio source", field_name);
    }
}

/* Read a numeric field while preserving the caller's stack depth. */
float read_number(lua_State *lua_state, const char *field_name, float default_value)
{
    float numeric_value = default_value;   /* Missing or non-numeric fields keep this. */
    lua_getfield(lua_state, -1, field_name);                     /* Push the field value. */
    if (lua_isnumber(lua_state, -1)) {
        numeric_value = static_cast<float>(lua_tonumber(lua_state, -1));
    }
    lua_pop(lua_state, 1);                                  /* Restore the stack. */
    return numeric_value;
}
