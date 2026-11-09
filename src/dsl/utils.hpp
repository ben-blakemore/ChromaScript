/* Translate Lua property values and named colours into scene data. */
#pragma once

#include <lua.hpp>

#include <array>

#include "def_types.hpp"

/* Write a named colour's normalised hue. Unknown names return false without changing
 * the output. */
bool hue_from_name(const char *name, float &hue_output);

/* Explicit HSV palette for named colours, including achromatic colours. */
bool hsv_from_name(const char *name, std::array<float, 3> &hsv_output);

/* The Marcotone hue for a pitch class, 0 = C through 11 = B. Out-of-range values
 * wrap. */
float hue_from_pitch_class(int pitch_class);

/* Convert HSV to the RGB the hardware wants, all components in [0, 1]. Hue wraps
 * past 1.0; saturation and value clamp. */
std::array<float, 3> hsv_to_rgb(float hue, float saturation, float brightness);

/* Return the marker used to expose and recognise a band binding. */
const char *band_marker(Band band);

/* The global name a band is exposed under in a script ("bass", "mid", ...). */
const char *band_name(Band band);

/* The sentinel strings for the two tone sources, planted into the 'tone' table as
 * 'tone.main' and 'tone.height'. Same contract as band_marker. */
const char *main_tone_marker();
const char *tone_height_marker();

/* The sentinel string for overall loudness, planted as the global 'loudness'. Same
 * contract as band_marker. */
const char *loudness_marker();

/* Property readers require the source table on top of the Lua stack and leave the
 * stack balanced. */

/* Read a constant or audio binding, using the fixed default for an absent field. */
void read_property(lua_State *lua_state, const char *field_name, float default_value, Property &property);

/* Override a supplied field; an absent field keeps the existing property. */
void override_property(lua_State *lua_state, const char *field_name, Property &property);

/* Parse the top stack value without popping it. Invalid values raise a
 * field-specific error; nil retains the default. */
void property_from_lua_value(lua_State *lua_state, const char *field_name, Property &property);

/* Read a numeric field, using the default when absent or non-numeric. */
float read_number(lua_State *lua_state, const char *field_name, float default_value);
