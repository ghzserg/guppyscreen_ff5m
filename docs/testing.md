# Testing

Integration (smoke) tests that run the real guppyscreen on your PC. They are a safety net for refactoring: if a change breaks startup, live data or a basic command, a test fails.

```sh
make test                         # build and run all tests
make test TEST='*homing*'         # run matching test cases (doctest name filter)
make test SANITIZE=1              # AddressSanitizer + UBSan build (separate output dir)
make -f tests/Makefile clean      # remove build-test/
```

The first run builds LVGL, libhv and spdlog for the PC (about a minute); after that only changed files are rebuilt. Each test takes about a second.

Host requirements: `gcc`/`g++`, `make`, `cmake`, all submodules checked out and patched (see `docs/build-toolchain.md`). No SDL and no tslib are needed.

## What is real and what is faked

Everything in `src/` is real: `GuppyScreen`, all panels and widgets, `State`, `Config`, the `KWebSocketClient`, and LVGL with the printer's `lv_conf.h` and `GUPPY_FF5M` set. Only the outside world is faked:

| External system | Fake | Where |
|---|---|---|
| Moonraker / Klipper | A real WebSocket JSON-RPC server on 127.0.0.1 (libhv) that answers from a fixture file, records every request and can push notifications | `tests/harness/fake_moonraker.*` |
| Framebuffer and backlight | In-memory 800×480 LVGL display; `fbdev_*` functions that record blank/brightness | `tests/harness/fake_hw.*` |
| Touch panel | An LVGL pointer device that `ui::tap()` presses and releases | `tests/harness/fake_hw.*` |
| Printer file system | Config, theme, log and thumbnail folder in a fresh temp dir (`$TMPDIR/guppy-test-XXXXXX`) | `tests/harness/app.cpp` |

`src/main.cpp` and `lv_drivers` are not part of the test build. The test's `fake_hw::hal_init` replaces `main.cpp`'s `hal_init`. The app source itself has no test-specific code.

## How a test runs

guppyscreen is built from process-wide singletons (`GuppyScreen`, `State`, `Config`, the static `ws`), so **each test case runs in its own process**. `tests/run_tests.sh` lists the test cases and runs them one by one, prints the output of failing ones and keeps every log in `build-test/<default|asan>/logs/`.

Inside a test, the test thread is the LVGL thread: nothing on screen changes unless the test calls `app::pump()` or `app::wait_until()`. The WebSocket client and the fake server run on their own libhv threads, as on the printer.

```cpp
TEST_CASE("homing panel sends G28") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);           // write config, GuppyScreen::init(), connect
  app::wait_ready(moonraker);      // subscribed and the init overlay is gone

  ui::tap_label("Homing");
  REQUIRE(app::wait_until([] { return ui::find_label("G28") != nullptr; }));

  ui::tap_label("G28");
  CHECK(app::wait_until([&] { return contains(moonraker.gcodes(), "G28"); }));
}
```

## Harness API

**`app::`** (`tests/harness/app.h`)
- `fixture(name)`: path of `tests/fixtures/<name>`.
- `start(moonraker)`: once per process.
- `wait_ready(moonraker)`: fails the test if startup doesn't finish in 10 s. Logs the requests the fixture didn't cover.
- `pump(ms)`: run the LVGL loop for `ms`.
- `wait_until(pred, timeout_ms = 5000)`: run the LVGL loop until `pred()` is true; returns `false` on timeout.
- `dir()`: the temp dir with `guppyconfig.json` and `guppyscreen.log`.

**`ui::`** (`tests/harness/ui.h`)
- `find_label(text, Match::Exact | Match::Contains)`: the first label that is **shown** (see below), or `nullptr`.
- `row_texts(text)`: texts of the labels next to label `text`, i.e. in its parent container. For example, a sensor row: name, value, "/", target.
- `texts_in(obj)`: shown label texts under `obj`.
- `tap(obj)`, `tap_label(text)`: press and release at the widget's centre through the fake touch panel.
- `dump_screen()`: all shown label texts. Use it in failure messages.
- `join(vector)`: format a list for failure messages.

"Shown" means a tap at the label's centre would reach it or the clickable container around it. The label must not be hidden, scrolled out, or **covered by another panel**. This matters because guppyscreen closes most panels by moving them behind the main screen (`lv_obj_move_background`), not by hiding them.

**`FakeMoonraker`** (`tests/harness/fake_moonraker.h`)
- `set_result(method, result)`: change an answer. Call it before `app::start()` for startup data.
- `push_status(status)`: send `notify_status_update`, e.g. `{{"extruder", {{"temperature", 215.0}}}}`.
- `push_notification(method, params)`: any other notification, e.g. `notify_gcode_response`, `notify_klippy_ready`.
- `gcodes()`: scripts sent through `printer.gcode.script`, in order.
- `requests(method)`, `request_count(method)`: every request's params.
- `unhandled()`: methods the fixture had no answer for. They are answered with `{}`.

## Fixtures

`tests/fixtures/ad5m_basic.json` maps a Moonraker method name to its `result`. It describes a basic AD5M in Moonraker's real formats:
- an extruder at 31.2 °C and a bed at 27.8 °C;
- `output_pin fan0`–`fan2`;
- a few macros;
- `configfile` with `config` and `settings`.

`printer.gcode.script` is always answered with `"ok"` and recorded.

The test config (written by `app::start`) shows the sensors "Extruder" and "Bed" and the three fans. It uses English names on purpose, so tests don't depend on the Russian defaults in `Config`. No translation catalog is loaded, so `_("...")` returns the English source string.

To cover other printers (e.g. AD5X with `zmod_color` and four extruders), add another fixture file. Ideally use responses recorded from a real printer.

## Adding tests

- Add a test as you refactor a code path, before changing it. Then confirm it fails if you break that path on purpose, as was done for the first three tests.
- Assert what the user sees or what reaches Moonraker: label texts and sent G-codes or requests. Don't assert on private panel state.
- Test case names must not contain commas (they are passed to doctest's `--test-case` filter).
- Prefer `wait_until` with a condition over fixed `pump` delays.
- Put new test files in `tests/*.cpp`; the Makefile picks them up.
- Not covered yet: thumbnails and calibration PNGs (`/opt/config/mod_data/` paths are hard-coded), anything that runs shell commands (Settings → restart), macros, console, spoolman.

## Sanitizers

`SANITIZE=1` builds into `build-test/asan/`. Memory errors (use-after-free, buffer overflow) abort the test and fail it. UBSan reports are only logged. Today the logs contain three reports, all inside LVGL: an integer overflow in `lv_chart.c:929/954` and a negative shift in `lv_font_fmt_txt.c:506`. Leak checking is off in practice, because the test binary exits with `_Exit` to skip the app's static destructors.
