-- Three circles share main-tone hue and tone-height saturation. Each band
-- independently drives its circle's brightness and radius.

circle{
  colour = { hue = tone.main, saturation = tone.height, value = bass.amplitude },
  x = -0.50,
  y = 0.0,
  radius = bass.amplitude,
}

circle{
  colour = { hue = tone.main, saturation = tone.height, value = mid.amplitude },
  x = 0.0,
  y = 0.0,
  radius = mid.amplitude,
}

circle{
  colour = { hue = tone.main, saturation = tone.height, value = treble.amplitude },
  x = 0.50,
  y = 0.0,
  radius = treble.amplitude,
}
