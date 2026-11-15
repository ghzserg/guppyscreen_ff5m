# Sensor tiles on Home Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Home shows the temperatures as tiles, 4 per row, with the print status on its own line under them, so every sensor, the status and the chart are visible on AD5M, AD5X and C5 Pro.

**Architecture:** Under `GUPPY_FF5M`, `SensorContainer` builds a tile instead of a row; `MainPanel` puts the tiles in a row-wrap container inside the temperature column, and the mini print status under that container in the same column (no longer inside the tiles' list). Without `GUPPY_FF5M` nothing changes.

**Tech Stack:** C++17, LVGL 8.3, doctest integration tests (`make test`).

**Spec:** `docs/superpowers/specs/2026-11-15-sensor-tiles-design.md`

## Global Constraints

- Everything new behind `#ifdef GUPPY_FF5M`; the upstream row stays in `#else`.
- Test names without commas.
- Labels of a tile stay direct children of the tile object: `ui::row_texts(name)` (print_sim_test) reads the texts of the name label's parent.
- Target: no "/", red (`lv_palette_main(LV_PALETTE_RED)`) when > 0, grey (`lv_palette_main(LV_PALETTE_GREY)`) when 0.
- Commit only when the user asks (project rule): commit steps below are "stage only" until then.

## Review Focus

- Reconnect: `create_sensors` clears and rebuilds the tiles; the status must stay under them, not between or above.
- A sensor without target (Head, Weight): no target label shown and no tap handler.
- Long display names ("Extruder", "Chamber"): clipped to the icon width, not widening the tile past 4 per row.
- Chamber heater tap still sends `_T_HEATER` (existing `handle_edit` branch) — unchanged code, but the tap target moved.
- Status hidden when idle: the column must not leave a gap that pushes anything.

---

### Task 1: Layout test for the three printers (RED)

**Files:**
- Modify: `tests/print_tab_test.cpp` (the two "print status under the temperatures" tests and their helper)
- Fixtures already present: `ad5m_basic.json`, `ad5x_basic.json`, `c5pro_basic.json`, `guppyconfig_ad5x.json`, `guppyconfig_c5pro.json`

**Interfaces:**
- Produces: helper `check_home_fits(FakeMoonraker&, const json &config_patch, std::vector<std::string> sensors)`; `printer_config(fixture_name, printer_name)` returning the patch with `monitored_sensors`, `fans` (and `default_macros` when present) under `printers/FF5M`, plus `prompt_emergency_stop: false`.

- [ ] **Step 1: Write the tests.** Replace the helper with `check_home_fits`: start, push printing, tap Home, wait for the "Status: printing" label; for each name in `sensors`, `ui::find_label(name)` is not null and its parent's area: `y2 < chart.y1` and `x2 < homing.x1`; status area: `y1 >= every sensor tile's y2`, `y2 < chart.y1`, `x2 < homing.x1`. Three cases:
  - `"Home fits the AD5M temperatures and print status"`: `ad5m_basic.json`, `{"Extruder","Bed"}` (test config's own sensors, display names "")—use the names the dump shows today.
  - `"Home fits the AD5X temperatures and print status"`: `ad5x_basic.json`, `printer_config("guppyconfig_ad5x.json","AD5X")`, `{"Extruder","Bed","Head","Weight"}`.
  - `"Home fits the C5 Pro temperatures and print status"`: `c5pro_basic.json`, `printer_config("guppyconfig_c5pro.json","C5Pro")`, `{"T0","T1","T2","T3","Bed","Chamber"}`.
- [ ] **Step 2: Run** `make test TEST='*Home fits*'`. Expected: AD5M PASS, AD5X and C5 Pro FAIL (status or sensor not found / under the chart).

### Task 2: Tiles (GREEN)

**Files:**
- Modify: `src/sensor_container.cpp`, `src/sensor_container.h`, `src/main_panel.cpp`, `src/main_panel.h`

**Interfaces:**
- Consumes: Task 1 tests.
- Produces: `MainPanel::sensor_tiles` (`lv_obj_t*`, FF5M only), the parent `create_sensors` passes to `SensorContainer`.

- [ ] **Step 1: `SensorContainer` tile under `GUPPY_FF5M`.** Size `LV_SIZE_CONTENT` both ways; stripe as now; icon zoom stays 150; name label under the icon in `lv_font_montserrat_12` (or notosemi_16 if Cyrillic is needed—check the ru names: use notosemi_16 at the theme size if montserrat lacks Cyrillic), `LV_LABEL_LONG_CLIP`, width = icon width; value and target labels stacked right of the icon (value on top); `divider_label` hidden; no border box on the target; tap handler on the tile as now. Target colour set in `update_target` (red > 0, grey 0) and in the constructor for the initial 0.
- [ ] **Step 2: `MainPanel::create_main` under `GUPPY_FF5M`.** `temp_cont` becomes a flex column (no scroll); new `sensor_tiles` child with `LV_FLEX_FLOW_ROW_WRAP`, width 100%, height content, small gaps; mini status reparented to `temp_cont` after `sensor_tiles` (not into the tiles). `create_sensors` passes `sensor_tiles` as parent. Keep `show()`'s `move_foreground` harmless: the status is the last child of `temp_cont` already. Remove the Task-0 reparent into `temp_cont`'s list that is in the working tree now.
- [ ] **Step 3: Run** `make test TEST='*Home fits*'`. Expected: 3 PASS.
- [ ] **Step 4: Run** `make test`. Expected: all pass except `layout snapshot` fixtures; regenerate with `UPDATE_SNAPSHOT=1 make test TEST='*layout*'` and check the diff touches only the temperature column.

### Task 3: Target colour and tap (tests for existing promises)

**Files:**
- Modify: `tests/print_tab_test.cpp` or a new `tests/sensor_tiles_test.cpp`

- [ ] **Step 1: Write** `"a sensor tile shows a set target in red and an unset one in grey"`: AD5M fixture; push `{"extruder": {"target": 200}}`; the Extruder tile's label "200" has text colour red; Bed's "0" grey. Run, see it pass (implemented in Task 2) — then break the colour in `update_target` locally and see it fail, restore.
- [ ] **Step 2: Write** `"a tap on a C5 Pro tile opens the numpad for that nozzle"`: C5 Pro fixture; `ui::tap_label("T2")`; numpad textarea shown; enter 210, READY; last gcode `SET_HEATER_TEMPERATURE HEATER=extruder2 TARGET=210`.
- [ ] **Step 3: Run** `make test`. Expected: all pass.

### Task 4: Look at it

- [ ] **Step 1:** Rebuild the preview (`make -f tests/Makefile preview`), restart the three preview servers (AD5M 18080, AD5X 18081, C5 Pro 18082), take Home-while-printing screenshots of the three and show them to the user.
- [ ] **Step 2:** Stage the changes; commit only when the user asks.
