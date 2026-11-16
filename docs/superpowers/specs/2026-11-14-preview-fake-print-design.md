# Preview: fake printing and live temperatures

## Intent
In the browser preview (`make preview`) the print file list is empty and the
temperatures never change, so the print screens and the temperature chart
can't be tried. The user wants to pick a file, start a print and watch the
print screen, with temperatures that behave like real heaters and show
correctly on the main screen chart.

## Goals
- A file list with metadata and thumbnails, editable in `preview/print_files.json`.
- `printer.print.start/pause/resume/cancel` drive a simulated print: heat up,
  then progress, layers, time, filament, Z, speed; `complete` at the end.
- Heaters follow their targets with a simple thermal model (heating, cooling,
  small noise), set by the print or by G-code (`SET_HEATER_TEMPERATURE`,
  `M104/M109/M140/M190`, `TURN_OFF_HEATERS`), reported ~4 times a second like Klipper.
- The chart gets a rising/falling curve (the app adds a point every ~2 s).

## Non-goals
- Real G-code parsing beyond heater commands; motion, homing, exclude-object.
- Changes to `src/`.

## Approach (chosen)
`FakePrinter` in `tests/harness/` on top of `FakeMoonraker`, so both the
preview and the tests use it. It answers the file/print methods, reacts to
heater G-codes (FakeMoonraker passes `printer.gcode.script` to a handler if
one is set), and pushes `notify_status_update` from its own thread.
A print lasts `--print-seconds` (default 120 s) of real time; `print_duration`
is scaled to the file's `estimated_time`. Thumbnails (a coloured cube, 32 and
300 px) are generated into `<run dir>/gcodes/.thumbs` at start.

Rejected: a static fixture with fixed notifications (no reaction to the UI);
real G-code files with slicer thumbnails (needs files in the repo, metadata parsing).

## Tests
`tests/print_sim_test.cpp`: file list shown; a print starts, progresses and
completes; heater target from G-code heats the fake extruder and the chart
series rises.
