-- Two circles demonstrate tone-height-driven saturation. Hue, brightness, radius and
-- position stay fixed.

-- Left: saturation follows the tone height. This is the ISCM mapping.
circle{
  colour = { hue = 0.0, saturation = tone.height, value = 1.0 },
  x = -0.5,
  y = 0.0,
  radius = 0.35,
}

-- Right: the same tone-height saturation binding at the opposite position.
circle{
  colour = { hue = 0.0, saturation = tone.height, value = 1.0 },
  x = 0.5,
  y = 0.0,
  radius = 0.25,
}
