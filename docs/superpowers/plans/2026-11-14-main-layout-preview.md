# Main Screen Layout + Local Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `layout.main` in `guppyconfig.json` moves/hides main-screen elements; `make preview` shows the real app in a browser.

**Architecture:** MainPanel reads `/layout/main` once in `create_main()` and applies it with LVGL grid calls. The preview links the app with the test harness fakes and a libhv HTTP server.

**Tech Stack:** C++17, LVGL 8 grid, nlohmann::json, libhv HttpServer, doctest harness.

**Spec:** `docs/superpowers/specs/2026-11-14-main-layout-preview-design.md`

## Global Constraints
- No `layout` key → identical positions to today.
- Reading the layout must not insert keys into `Config` data.
- No preview-only code in `src/`.
- Item ids: `temps`, `chart`, `homing`, `extrude`, `fans`, `led`, `print`, `stop`.

## Review Focus
- `layout` present but not an object / item with wrong-typed field → defaults, warning, no crash (test: `layout with wrong types keeps defaults`).
- `col`/`row` outside the grid → clamped (test: `out of range cell is clamped`).
- Config without `layout` saved by SysInfo → no `layout` key appears (test: default case checks `Config` data has no `/layout`).
- Bad JSON in the preview config → error shown, preview keeps the last good screen.
- Columns list empty/invalid → falls back to default 4×3.

---

### Task 1: Pin today's layout, then implement `layout.main`

**Files:**
- Modify: `tests/harness/app.h`, `tests/harness/app.cpp` — `void start(FakeMoonraker &, const json &config_patch = json::object())`, merge_patch into the test config.
- Create: `tests/layout_test.cpp`
- Create: `src/grid_layout.h`, `src/grid_layout.cpp`
- Modify: `src/main_panel.h`, `src/main_panel.cpp:229-284`

**Interfaces:**
- Produces: `class GridLayout { GridLayout(const json &layout /* may be null */, size_t def_cols, size_t def_rows); void apply_tracks(lv_obj_t *cont); void place(lv_obj_t *obj, const std::string &id, int col, int row, int col_span = 1, int row_span = 1, lv_grid_align_t align = LV_GRID_ALIGN_CENTER); }` — owns the descriptor vectors; MainPanel keeps it as a member.

- [ ] Tests (label centres via `lv_obj_get_coords` of the button's container): `default layout matches the hard-coded grid` (Homing/Fans/Print share x, Extrude/LED/Stop share x and are right of them; Homing above Fans above Print; no `/layout` in Config data); `layout moves and hides buttons` (`led.hidden` → `find_label("LED") == nullptr`; `fans` at col 3 row 1 → same x as Extrude); `layout with wrong types keeps defaults`; `out of range cell is clamped`.
- [ ] Run `make test TEST='*layout*'`: default passes, others fail.
- [ ] Implement GridLayout and use it in `create_main()`.
- [ ] Run `make test`: all pass. Break `place()` on purpose once, see the layout tests fail.

### Task 2: Browser preview

**Files:**
- Create: `preview/preview_main.cpp`, `preview/page.html` (embedded via `#include` raw string or read at runtime), `preview/guppyconfig.json` (example with layout), `preview/run.sh` (restart loop)
- Modify: `tests/Makefile` — `preview` target building `$(OUT)/guppy_preview` from CSRCS + APP_SRCS + `fake_hw.cpp`, `fake_moonraker.cpp`, `png_writer.cpp` + `preview/*.cpp`; `Makefile` — `preview:` delegating to it.

**Interfaces:**
- Consumes: `fake_hw::hal_init`, `fake_hw::touch_set`, `fake_hw::pixel`, `FakeMoonraker`.
- HTTP: `GET /` page; `GET /frame.png` (PNG of the framebuffer); `GET /status` (`{"frame": n, "error": "..."}`); `POST /touch` `{"x","y","down"}`.

- [ ] `preview/run.sh <config>`: validates JSON, starts `guppy_preview`, restarts it when the config's mtime changes; on bad JSON keeps the old process and writes the error to a file the child serves in `/status`.
- [ ] Verify: start, fetch `/frame.png` into the scratchpad and look at it with default and example configs; `POST /touch` on "Homing" → frame shows the homing panel; `make test` still links and passes.
