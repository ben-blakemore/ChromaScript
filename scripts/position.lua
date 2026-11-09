-- Compare vertical and horizontal tone-height bindings with a loudness-driven
-- position. Fixed coordinates use [-1, 1]; tone height retains [0, 1], while other
-- audio sources span the full axis.

-- Left: tone height drives y; radius and colour stay fixed.
circle{
  colour = { hue = 0.0, saturation = 1.0, value = 1.0 },
  x = 0,
  y = tone.height,
  radius = 0.15,
}

circle{
  colour = { hue = 0.5, saturation = 1.0, value = 1.0 },
  x = 0.3,
  y = 0,
  radius = 0.1,
}