-- A grid of circles with randomly selected band bindings. Red = bass, green = mid and
-- blue = treble. Lua builds the scene once; the radius bindings update per frame.

local column_count = 6      -- circles across
local row_count = 6      -- circles down
local margin = 0.85  -- how far towards the edges the grid extends

-- The three bands, paired with a colour so each shape's source is visible.
local bands = {
  { source = bass.amplitude,   colour = "red"   },
  { source = mid.amplitude,    colour = "green" },
  { source = treble.amplitude, colour = "blue"  },
}

for row_index = 0, row_count - 1 do
  for column_index = 0, column_count - 1 do
    -- Evenly space the grid across [-margin, margin] in both axes.
    local horizontal_position = -margin + 2 * margin * column_index / (column_count - 1)
    local vertical_position = -margin + 2 * margin * row_index / (row_count - 1)

    -- Randomly pick which band this circle reacts to.
    local selected_band = bands[math.random(#bands)]

    circle{ colour = selected_band.colour, x = horizontal_position,
            y = vertical_position, radius = selected_band.source }
  end
end
