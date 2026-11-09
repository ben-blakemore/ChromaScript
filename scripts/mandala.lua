-- Five concentric rings: bass at the centre, mid between and treble at the rim

-- Each ring has a reactive halo below its fixed-size rim and inner shape

local aspect_ratio = 2560 / 1440   -- x is compressed by this, so the rings stay round

-- Draw a ring in three passes: reactive halos, fixed outer shapes and smaller inner
-- shapes. The inner radius is a fraction of the outer radius.
local function band(outer_shape, inner_shape, shape_count, ring_radius, shape_radius,
                    inner_radius_fraction, rotation_radians, hue_source, brightness_source)
  -- Draw reactive halos first so the fixed rims cover them at low levels.
  for shape_index = 0, shape_count - 1 do
    local angle_radians = rotation_radians + 2 * math.pi * shape_index / shape_count
    outer_shape{
      colour = { hue = hue_source, saturation = 1.0, value = 1.0 },
      x = math.cos(angle_radians) * ring_radius / aspect_ratio,
      y = math.sin(angle_radians) * ring_radius,
      radius = brightness_source,   -- bound, so this is what changes size
    }
  end
  -- Outer pass: the rims.
  for shape_index = 0, shape_count - 1 do
    local angle_radians = rotation_radians + 2 * math.pi * shape_index / shape_count
    outer_shape{
      colour = { hue = hue_source, saturation = tone.height, value = brightness_source },
      x = math.cos(angle_radians) * ring_radius / aspect_ratio,
      y = math.sin(angle_radians) * ring_radius,
      radius = shape_radius,
    }
  end
  -- Overpaint each rim with a smaller, fully saturated shape.
  for shape_index = 0, shape_count - 1 do
    local angle_radians = rotation_radians + 2 * math.pi * shape_index / shape_count
    inner_shape{
      colour = { hue = hue_source, saturation = 1.0, value = brightness_source },
      x = math.cos(angle_radians) * ring_radius / aspect_ratio,
      y = math.sin(angle_radians) * ring_radius,
      radius = shape_radius * inner_radius_fraction,
    }
  end
end

-- Draw outermost rings first
band(octagon,  pentagon,   32,  0.90,  0.088, 0.50, 0.0,           tone.main,       treble.amplitude)
band(hexagon,  triangle,   22,  0.68,  0.098, 0.52, math.pi / 22,  tone.height,     treble.amplitude)
band(pentagon, circle,     15,  0.49,  0.105, 0.46, math.pi / 15,  tone.height,     mid.amplitude)
band(square,   circle,     11,  0.32,  0.092, 0.44, math.pi / 11,  bass.amplitude,  mid.amplitude)
band(triangle, circle,      7,  0.17,  0.082, 0.40, math.pi / 2,   bass.amplitude,  bass.amplitude)

-- Fixed centre disc using the main-tone colour.
circle{
  colour = { hue = tone.main, saturation = tone.height, value = 1.0 },
  x = 0.0,
  y = 0.0,
  radius = 0.085,
}