# Guppy Screen for FlashForge

[Guppy Screen](https://github.com/ballaswag/guppyscreen) is a standalone LVGL touchscreen interface for Klipper and Moonraker, without X11 or Wayland. This fork is adapted for FlashForge printers running [Z-Mod](https://github.com/ghzserg/zmod).

[Русская версия](README_RU.md)

## Supported printers

- FlashForge Adventurer 5M (AD5M)
- FlashForge Adventurer 5M Pro (AD5M Pro)
- FlashForge Adventurer 5X (AD5X)
- FlashForge Creator 5 (C5)
- FlashForge Creator 5 Pro (C5 Pro)

The availability of controls depends on the printer hardware and Klipper objects.

## Enable Guppy Screen

Install **Z-Mod** first. In the Fluidd or Mainsail console run:

```gcode
DISPLAY_OFF
```

Guppy Screen is enabled by default; no additional arguments are needed. To restore the factory screen:

```gcode
DISPLAY_ON
```

Before disabling the stock screen, familiarize yourself with bed leveling, Z-offset and start/end print macros. Guppy Screen does not provide on-screen Wi-Fi configuration.

See [the Z-Mod DISPLAY_OFF documentation](https://wiki.zmod.link/System/#display_off).

## Features

Core features from the original Guppy Screen are retained: temperature and motion controls, fans, LEDs, file printing, console, macros, Bed Mesh, Input Shaper, belt calibration, Fine Tune and Spoolman integration.

### Additions and changes compared with ballaswag/guppyscreen

**FlashForge and Z-Mod support**

- FlashForge display and touch support, with ARM (AD5M) and MIPS (AD5X) builds.
- Multiple extruders and tool selection on Creator 5 Pro; the AD5X's four filament channels share one physical nozzle.
- Z-Mod filament load/unload, colour changes (**COLOR**), **ColdPull** and tool selection.
- Air circulation and damper control on AD5M Pro and C5 Pro: internal, external and stop.
- Additional controls appear only when their sensors, fans or Klipper macros are available.

**Printing and calibration**

- **PID calibration** for the nozzle and bed, including temperature and nozzle selection.
- **Nozzle cleaning**, **load-cell tare** and weight readouts on supported printers.
- **Bed screw adjustment** using `BED_LEVEL_SCREWS_TUNE`.
- **Firmware Retraction** controls for retract/unretract lengths and speeds.
- **Exclude Object** via a list or an interactive object map with tap-to-exclude, zoom and pan.
- **Extended Bed Mesh** with height deviation display, move-to-point by tapping the mesh and standard or automatic full-bed calibration.
- **Extended motion Limits** for velocity, acceleration, Square Corner Velocity and either `minimum_cruise_ratio` (new Klipper) or `max_accel_to_decel` (older Klipper).
- **Extruder** and **VFA** calibration buttons in Tune when their macros exist; belt and Input Shaper calibration adapted for Z-Mod.
- Controls for **interrupted-print recovery**, **restoring the factory screen**, **switching Fluidd/Mainsail** and powering off the printer.

**Interface**

- Persistent **Home / Print / Tune / Settings** sidebar with a separate emergency-stop button.
- Sensor and temperature tiles with current and target values; separate nozzle tiles for multi-tool printers.
- Blue highlight on the tile being edited and a brief confirmation highlight for changes in Fine Tune.
- Fine Tune and Bed Mesh available from Home; Console and Macros under Settings.
- Reworked printing screen with larger values, filename, thumbnail, **Files** button, wide progress bar, ETA and elapsed time.
- **Current acceleration limit** on the print screen; tap to open Limits.
- Layouts adapted to the 800×480 display, including Fine Tune, Limits, Retract, nozzle selection and fans.
- **Screen brightness** control in system settings.

**Fonts and languages**

- **Noto Sans** fonts with Cyrillic and extended Latin glyphs, allowing filenames to be displayed using more than English letters.
- Interface translation files for **12 languages**: Russian, English, German, French, Spanish, Italian, Portuguese, Czech, Turkish, Chinese, Japanese and Korean.
- Translations are stored as `.po` files; `.mo` files are built for releases. Character rendering is limited to available font glyphs; complete coverage of all writing systems is not claimed.

**Performance and reliability**

- Reduced RAM consumption (several megabytes saved in comparative AD5M measurements).
- Faster interface operation and fewer unnecessary redraws.
- Fixes for thumbnails, missing metadata, print-state updates, Moonraker reconnection and large replies.
- Automated integration tests with a simulated Moonraker, including ASan/UBSan checks.

### Differences in feature availability

This fork targets a single FlashForge printer with Z-Mod. Some original Guppy Screen panels are not included in this build: Wi-Fi configuration, multi-printer selection, TMC settings and external power-device controls. Guppy Screen is updated through Z-Mod.

## Downloads

[FlashForge Guppy Screen releases](https://github.com/ghzserg/guppyscreen_ff5m/releases).

AD5M and AD5X packages are built separately using the appropriate Buildroot SDKs.

## Build and test

```sh
git clone --recursive https://github.com/ghzserg/guppyscreen_ff5m.git
cd guppyscreen_ff5m
bash ci/apply-patches.sh ad5m  # or ad5x; apply once to clean submodules
make test
```

See [build script](ci/build-release.sh) and [testing documentation](docs/testing.md).

## Credits

**Special thanks to [mlengle](https://github.com/mlengle)** for extensive work on the user interface, bug fixes, automated tests, real-printer testing, memory measurements and carefully prepared improvements.

Thanks to [ballaswag](https://github.com/ballaswag/guppyscreen) and all contributors to the original Guppy Screen, as well as the developers of [LVGL](https://github.com/lvgl/lvgl), [Klipper](https://github.com/Klipper3d/klipper) and [Moonraker](https://github.com/Arksine/moonraker).

[License](LICENSE) · [Compare with upstream](https://github.com/ballaswag/guppyscreen/compare/main...ghzserg:main)