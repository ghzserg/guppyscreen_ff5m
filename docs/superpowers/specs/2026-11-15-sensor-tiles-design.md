# Sensor tiles on Home (FF5M)

## Why

Home lists the temperatures as 330×60 px rows (icon, name, current, "/",
target in a bordered box). The list has about 296 px, and the chart covers
its lower part. AD5M (2 sensors) fits, AD5X (Extruder, Bed, Head, Weight)
does not, C5 Pro (T0–T3, Bed, Chamber) hides Bed and Chamber under the
chart. The print status, now the last item of the list, ends up hidden too.
Goal: every sensor, the print status and the chart visible on Home without
scrolling, on AD5M, AD5X and C5 Pro.

Sensor sets come from zMod's configs: `tests/fixtures/guppyconfig_ad5x.json`,
`tests/fixtures/guppyconfig_c5pro.json`.

## Approaches

- **Tiles, 4 per row (chosen).** Icon with a short name under it, current and
  target stacked to its right. 4 tiles of ~80 px fit the 356 px column; 6
  sensors take 2 rows (~100 px).
- Shorter rows, one per line (~44 px). 6 sensors still take ~270 px: C5 Pro
  keeps scrolling.
- Tiles 3 per row. Fits more easily, but C5 Pro's four nozzles would not
  share a row.

## Design

- `SensorContainer` (used only by Home) becomes a tile under `GUPPY_FF5M`;
  without it the row stays as it is.
- Tile: colour stripe on the left (as now), the sensor icon at its current
  size (zoom 150, ~38 px), the display name under it in a small font, clipped
  to the icon's width. To the right, two numbers stacked: current on top,
  target below, no "/".
- Target colour: red when the target is above 0, grey when it is 0. Sensors
  without a target (not controllable, no show_target) show only the current
  value.
- A tap on the tile opens the numpad, as a tap on the row does now. The
  bordered box around the target goes away.
- The temperature column wraps tiles 4 per row (flex row wrap).
- The print status is its own line under the tiles, outside the tiles'
  container, so it is never pushed out of view; the chart stays below it.

## Left to the builder

Exact paddings and gaps, the small font for the name, how the column's
height is split between tiles, status and chart (tiles take what they need).

## Checks

- Tests: on AD5M, AD5X and C5 Pro fixtures, every sensor tile and the print
  status are on screen and above the chart, left of the buttons.
- A tap on a tile opens the numpad for that heater.
- Target colour follows the target (0 grey, above 0 red).
- Screenshots of the three printers in the preview.
