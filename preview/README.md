# Preview: guppyscreen on the PC, in a browser

```sh
make preview                                    # build, then run on http://localhost:8080
make preview PREVIEW_ARGS='--port 8081 --config /path/to/guppyconfig.json --print-seconds 30'
```

Open `http://localhost:8080` (from Windows too, when running in WSL2). The mouse works as the touch panel. Stop with Ctrl+C.

It runs the real app (all of `src/`, the printer's `lv_conf.h`, `GUPPY_FF5M`, `GUPPY_SMALL_SCREEN`). Only the outside world is fake, the same fakes the tests use (`tests/harness/`):
- **Moonraker**: answers from `tests/fixtures/ad5m_basic.json` (`--fixture` to change): printer objects, three fans, a few macros.
- **Printer** (`tests/harness/fake_printer.*`): G-code files you can print, and heaters that follow their targets. See below.
- **Display**: 800×480 in memory, served to the page as frames.

## Editing on the page

Next to the screen there is an editor with two tabs, `guppyconfig.json` and `print_files.json`. **Применить** (or Ctrl+S) checks the text, saves it to the file and restarts the screen with it. If the text isn't valid, it stays in the editor and the error is shown under it; nothing is written. **Отменить правки** goes back to what is on disk. A dot on a tab marks unsaved edits. Edits made to the files elsewhere show up in the editor after the restart, unless that tab has unsaved edits.

## Config

Edit `preview/guppyconfig.json` (or the file given with `--config`), on the page or in any editor. On every save the app restarts with it, and the page picks up the new screen. If the JSON doesn't parse, the error shows on the page and in the terminal, and the last good screen stays.

The file is not changed by the app. It is copied to `build-test/preview-run/guppyconfig.json` with the PC's paths and the fake Moonraker's port, and settings changed in the UI go to that copy. Logs are in `build-test/preview-run/guppyscreen.log`.

### `layout`: screen grids

Every screen laid out as a grid can be changed from the config, under `layout.<screen>`:

```json
"layout": {
  "main": {
    "columns": [1, 1, 1, 1],
    "rows": [1, 1, 1],
    "items": {
      "led":  {"hidden": true},
      "fans": {"col": 2, "row": 1, "col_span": 2}
    }
  },
  "homing": {"items": {"motoroff": {"hidden": true}}}
}
```

| Key | Values |
|---|---|
| `columns`, `rows` | list of tracks: a number 1..99 is a share (`fr`), `"content"` sizes to the content, `"80px"` is fixed |
| `padding` | space inside the screen's grid, px: a number for all sides or `{"top", "right", "bottom", "left"}` (any of them). In an item: space inside that element, moving its content but not its cell (e.g. `"temps": {"padding": {"top": 6}}` lowers the temperature tiles and the print status; 6 is Home's default) |
| `gap` | space between columns and rows, px: a number or `{"row", "column"}` |
| `col`, `row` | cell, from 0 |
| `col_span`, `row_span` | cells taken |
| `hidden` | `true` hides the element |
| `align` | `center`, `start`, `end`, `stretch`: both directions; `align_x`, `align_y` set one |

Screens and their items (the preview page lists them too, with where each one is now):

| Screen | What | Items |
|---|---|---|
| `homing` | Homing (move axes) | `home_all`, `y_up`, `home_xy`, `z_up`, `x_down`, `y_down`, `x_up`, `z_down`, `motoroff`, `distance_selector` |
| `settings` | Settings tab | `restart_klipper`, `restart_firmware`, `guppy_restart`, `sysinfo`, `power`, `display_on`, `web`, `zrestore`, `macros`, `console` |
| `finetune` | Fine Tune (during a print) | `zreset`, `zup`, `zdown`, `zoffset_selector`, `pareset`, `paup`, `padown`, `speed_reset`, `speed_up`, `speed_down`, `multiplier_selector`, `flow_reset`, `flow_up`, `flow_down`, `values` |
| `pro` | Pro | `air_circulation_internal`, `air_circulation_external`, `air_circulation_stop`, `calibrate_extruders`, `calibrate_vfa` |
| `pid` | PID / calibration | `temps`, `chart`, `pid_extruder`, `pid_bed`, `clear_nozzle`, `bed_level_screws_tune`, `load_cell_tare` |
| `print_status_details` | print status: the value tiles | `extruder_temp`, `bed_temp`, `print_speed`, `z_offset`, `flow_rate`, `layers`, `elapsed`, `fan0`, `time_left`, `filament` |
| `print_status` | print status: thumbnail, details, progress bar, buttons | `thumbnail`, `details`, `progress`, `buttons` |
| `file_info` | print file list: the selected file | `thumbnail`, `name`, `details` |
| `print_files` | print file list (the Print tab when idle): the file and its buttons | `file_info`, `print` |
| `extruder` | Extruder | `leftside_btns`, `spoolman`, `temp_selector`, `length_selector`, `speed_selector`, `rightside_btns` |
| `prompt` | Klipper prompt dialog | `header`, `body`, `footer` |
| `inputshaper` | Input Shaper | `xspinner`, `yspinner`, `xgraph`, `ygraph`, `xoutput`, `youtput`, `xcontrol`, `ycontrol`, `buttons` |
| `belts` | Belts calibration | `graph`, `spinner`, `excite_control`, `buttons` |
| `tune` | Tune tab | `limits`, `retract`, `inputshaper`, `belts_calibration`, `pro`, `pid` |
| `main` | main screen | `homing`, `extrude`, `fans`, `led`, `finetune`, `bedmesh`, `temps`, `chart` |

`preview/guppyconfig.json` has every screen written out with its built-in values. To get that block for another config (e.g. the printer's), run `build-test/default/guppy_preview --dump-layout --config <file>`: it prints the `"layout"` of every screen as it is with that config.

Anything left out keeps the built-in position. Wrong types are logged as warnings and ignored. Cells given in the config, and all cells once `columns` or `rows` are set, are clamped to the grid. Two elements in one cell are drawn on top of each other.

How far an element sits from the grid's edge adds up from: the `padding`, the `gap`, the size of its cells (the container's size and the tracks; every screen starts right of the 60 px navigation bar and is 740 px wide), and its alignment in the cells (a centred button sits half the spare space in). To line elements up across screens, give them the same `padding` and align them to the cell's edge (`"align_x": "end", "align_y": "start"`).

Elements keep their own size: most are fixed or a share of the whole screen, not of their cells. Add `"align": "stretch"` to make one fill its cells.

Some elements are shown and hidden by the app itself: the graphs and spinners of the calibration screens (`belts`: `graph`, `spinner`; `inputshaper`: `xgraph`, `ygraph`, `xspinner`, `yspinner`). For these, `"hidden": true` holds only until a calibration shows them.

The same `layout` works in the printer's `guppyconfig.json`.

## Printing and temperatures

The files in the print list come from `preview/print_files.json` (`--files` for another one). Like the config, the file is watched and the app restarts when it changes.

```json
[{"path": "parts/bracket.gcode", "estimated_time": 3900, "filament_total": 6100, "filament_weight_total": 18.2,
  "layer_height": 0.16, "object_height": 12, "extruder_temp": 225, "bed_temp": 65, "color": "#1e88e5"}]
```

`path` is required. A `/` makes a folder. `estimated_time` is in seconds, `filament_total` in mm and `filament_weight_total` in g. `color` is the colour of the generated thumbnail cube. Temperatures are limited to what the fake heaters reach: `extruder_temp` 0..300, `bed_temp` 0..120.

**What happens when you press Print:**
1. The bed and the nozzle heat up to the file's temperatures, in about 40 s.
2. The print runs for `--print-seconds` of real time (default 120). Time, filament, layer, Z, speed and the part fan follow the file's figures, with time scaled to its `estimated_time`.
3. At the end the state is `complete` and the heaters cool down.

Pause, Resume and Cancel work.

**Heaters** (extruder and bed) follow their targets with a simple thermal model: full power until close, then holding, plus slow cooling and a little noise. Klipper sends updates 4 times a second, and so does the fake. They react to the targets set from the UI and to `SET_HEATER_TEMPERATURE`, `M104`, `M109`, `M140`, `M190` and `TURN_OFF_HEATERS`. Other G-code is accepted and ignored. The main screen chart adds a point every ~2 s.

## HTTP

The server answers only its own page: the `Host` must be `localhost`, `[::1]` or an IP address with the preview's port, and an `Origin`, if sent, must be that same address. Other web pages open in the browser can't use it (POST /file writes files in the working tree), and neither can a site whose own name points at 127.0.0.1. With `--host 0.0.0.0` the page also works at the machine's IP address.

| Request | |
|---|---|
| `GET /` | the page (`preview/page.html`, read on each request) |
| `GET /status` | `{"frame": n, "error": "...", "config": "...", "files": "...", "fixture": "...", "pid": n}` |
| `GET /frame.rgba[?since=n]` | the screen, 800×480 RGBA; 204 if unchanged since frame `n` |
| `GET /frame.png` | the screen as PNG |
| `GET /file?name=config\|files` | the text of the config or of the print files list |
| `POST /file?name=config\|files` | body: the new text. Saved only if valid; `{"ok": true}` or 400 with `{"ok": false, "error": "..."}` |
| `POST /touch` | `{"x": 0..799, "y": 0..479, "down": true}` |
| `POST /tap` | `{"x": ..., "y": ...}`: press and release |
