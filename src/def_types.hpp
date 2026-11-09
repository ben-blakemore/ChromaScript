/* Shared scene and audio types used by the DSL, analyser and renderer. */
#pragma once

#include <vector>

/* Published frequency bands. BandCount sizes all band-indexed containers. */
enum Band { BandBass, BandMid, BandTreble, BandCount };

/* The constant or audio feature supplying a visual property. */
enum class SourceKind { Fixed, BandAmplitude, MainTone, ToneHeight, Loudness };

/* A constant or audio binding, resolved per frame. Keep this resource-free: Lua
 * errors use longjmp and skip C++ destructors. */
struct Property {
    SourceKind kind  = SourceKind::Fixed;
    float      fixed = 0.0f;     /* used when kind == Fixed */
    Band       band  = BandBass; /* used when kind == BandAmplitude */
};

/* The geometric kinds a shape can take. All are drawn as regular polygons on a
 * circumscribed circle, so 'radius' means the same thing for each. */
enum class ShapeKind {
    Circle,
    Triangle,
    Square,
    Pentagon,
    Hexagon,
    Octagon,
};

/* Independent HSV properties allow separate audio bindings. The renderer converts to
 * RGB after smoothing. */
struct Colour {
    Property hue;          /* [0, 1] around the wheel, so 1.0 wraps to 0.0 */
    Property saturation;   /* [0, 1], 0 = grey, 1 = fully saturated */
    Property value;        /* [0, 1] brightness, 0 = black */
};

/* One shape in the scene. */
struct Shape {
    ShapeKind kind = ShapeKind::Circle;
    Colour    colour;                        /* fixed or bound, per component */

    /* Fixed coordinates are literal NDC values in [-1, 1]. Tone height retains [0,
     * 1]; other audio sources span the full axis. */
    Property  horizontal_position, vertical_position;
    Property  radius;                        /* fixed or bound */
};

/* The script builds this scene; each successful reload replaces its shapes. */
struct Scene {
    std::vector<Shape> shapes;

    void clear() { shapes.clear(); }
    std::size_t count() const { return shapes.size(); }
};
