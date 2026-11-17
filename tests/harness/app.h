#ifndef GUPPY_TEST_APP_H
#define GUPPY_TEST_APP_H

// Runs the real guppyscreen (GuppyScreen and everything under it) inside the
// test process, connected to a FakeMoonraker.
//
// guppyscreen is built from process-wide singletons, so the app can be
// started only once per process. tests/run_tests.sh runs every test case in
// its own process.
//
// The test thread is the LVGL thread: nothing in the UI moves unless the
// test calls pump() or wait_until().

#include "fake_moonraker.h"

#include <functional>
#include <string>

namespace app {

// Absolute path of a file in tests/fixtures/.
std::string fixture(const std::string &name);

// Write a test config pointing at `moonraker`, then run GuppyScreen::init().
// Config, themes and logs go to a fresh temporary directory.
void start(FakeMoonraker &moonraker);

// Directory start() created (contains guppyconfig.json, guppyscreen.log, ...).
const std::string &dir();

// Run the LVGL loop (timers, input, redraw) for `ms` milliseconds.
void pump(int ms);

// Runs due LVGL timers once and draws everything that is invalid now, at
// once (no waiting): use it to look at pixels after changing objects.
void redraw();

// Run the LVGL loop until `pred` returns true. Returns false on timeout.
// `pred` runs on the test thread without the LVGL lock; ui:: helpers take it.
bool wait_until(const std::function<bool()> &pred, int timeout_ms = 5000);

// Wait until startup has finished: the printer objects are subscribed and
// the "Waiting for the Klipper launch" overlay is gone. Fails the test on timeout.
void wait_ready(FakeMoonraker &moonraker);

// Pumps until the app has sent Moonraker no new request for `quiet_ms` (at
// most `timeout_ms`): the app has handled what it was waiting for. Instead
// of a fixed pump() after a reconnect or a reply that triggers more work.
void settle(FakeMoonraker &moonraker, int quiet_ms = 100, int timeout_ms = 5000);

}  // namespace app

#endif  // GUPPY_TEST_APP_H
