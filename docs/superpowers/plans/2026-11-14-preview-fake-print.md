# Preview Fake Print Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** fake print files, a simulated print and heater model behind FakeMoonraker, used by the preview and tests.

**Architecture:** `FakePrinter` (tests/harness) installs results/handlers on a FakeMoonraker and runs a tick thread pushing status updates.

**Tech Stack:** C++17, nlohmann::json, libhv (via FakeMoonraker), doctest.

**Spec:** `docs/superpowers/specs/2026-11-14-preview-fake-print-design.md`

## Global Constraints
- No changes in `src/`.
- FakeMoonraker handlers run under its mutex: they must not call FakeMoonraker.
- Status pushes only while a client is connected.

## Review Focus
- G-code with several lines / lower case / unknown heater → only known heaters change.
- print.start while printing → ignored.
- Cancel during heating → targets 0, state cancelled.
- print_files.json invalid → preview reports it like a bad config.
- App restart (preview) while printing → new process starts idle.

---

### Task 1: FakePrinter + tests

**Files:** Create `tests/harness/fake_printer.{h,cpp}`, `tests/print_sim_test.cpp`; Modify `tests/harness/fake_moonraker.cpp` (gcode handler).

**Interfaces (produced):**
- `struct PrintFile { std::string path; int estimated_time; double filament_total, filament_weight_total, layer_height, object_height; int extruder_temp, bed_temp; uint32_t color; }`
- `std::vector<PrintFile> FakePrinter::parse_files(const json &)` (throws std::runtime_error with a message)
- `FakePrinter(FakeMoonraker &, std::vector<PrintFile>, Options{double print_seconds=120; double heat_speed=1; int tick_ms=250; std::string gcodes_dir;})`, destructor stops the thread.

- [ ] Tests: `fake printer files are listed in the print panel`; `fake print runs from start to complete`; `heater gcode heats the fake extruder and the chart rises`.
- [ ] Run, see them fail (no FakePrinter).
- [ ] Implement; `make test` all green.

### Task 2: Preview integration

**Files:** Create `preview/print_files.json`; Modify `preview/preview_main.cpp`, `preview/README.md`.

- [ ] `--files FILE` (default preview/print_files.json), `--print-seconds N`; files file watched like the config and validated with `parse_files`.
- [ ] Verify: preview starts, restart on files change, bad files JSON reported; `make test` green.
