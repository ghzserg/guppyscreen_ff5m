# Main screen layout from config + local preview

## Intent
The user wants to change the position and visibility of the main screen's
elements through `guppyconfig.json`, and to see the result on their PC
(WSL2, no SDL2, no X/Wayland display) without flashing the printer.

## Goals
- `layout.main` in `guppyconfig.json`: grid tracks and per-item cell, span, hidden.
- Without `layout` the screen looks exactly as today.
- A local preview: the real app on the PC, its screen in a browser, clickable,
  restarted automatically when the edited config changes.

## Non-goals
- Layout for other panels (only the main screen for now).
- Realistic printer data in the preview (static fixture `ad5m_basic.json`).

## Anti-goals
- Config without `layout` crashes or gets `null` keys written back by `Config::save()`.
- Preview-specific code in `src/`.

## Approaches
1. **Browser preview on top of the test harness** (chosen): in-memory display,
   fake touch and `FakeMoonraker` from `tests/harness/`, libhv `HttpServer`
   serves the frame and takes mouse input. Works with nothing installed.
2. SDL2 simulator build (`make` without `CROSS_COMPILE`): needs `sudo apt install
   libsdl2-dev`, a working WSLg, a real Moonraker or a separate fake. Rejected.

## Design
- `layout.main`:
  `{"columns": [1,1,1,1], "rows": [1,1,1], "items": {"<id>": {"col", "row", "col_span", "row_span", "hidden", "align"}}}`.
  Track: number = `fr`, `"content"`, or `"<n>px"`. Items: `temps`, `chart`,
  `homing`, `extrude`, `fans`, `led`, `print`, `stop`. Missing keys keep the
  hard-coded position; wrong types are logged and ignored; cells are clamped to the grid.
  The `/layout` subtree is read without inserting keys into `Config` data.
  Grid descriptor arrays live in `MainPanel` members (LVGL keeps the pointer).
- Tests (`tests/layout_test.cpp`): default positions, moved item, hidden item, span.
- Preview (`preview/`, `make preview`): `guppy_preview` binary = app sources +
  `fake_hw` + `fake_moonraker` + own main with an HTTP server on `0.0.0.0:8080`.
  The user's `preview/guppyconfig.json` is copied to a run dir with overrides
  (Moonraker port, log, thumbnails, no screen sleep); a parent loop restarts the
  child when the file changes; bad JSON is reported in the terminal and on the page.

## Left to the builder
File and function names, page markup, frame transport.
