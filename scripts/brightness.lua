-- Compare overall loudness and bass amplitude as brightness sources. Other properties
-- stay fixed.

-- Left: brightness follows the overall loudness. This is the ISCM mapping.
circle{
  colour = { hue = 0.0, saturation = 1.0, value = loudness},
  x = -0.5,
  y = 0.0,
  radius = loudness.amplitude,
}

-- Right: brightness follows the bass band instead of overall loudness.
circle{
  colour = { hue = 0.5, saturation = 1.0, value = 1.0},
  x = -.25,
  y = 0.0,
  radius = 0.15,
}
