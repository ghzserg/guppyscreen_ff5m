#ifndef GUPPY_TEST_FAKE_HW_H
#define GUPPY_TEST_FAKE_HW_H

// Fakes for the printer's display and touch hardware.
//
// The test build does not compile lv_drivers. Instead this module provides
// the fbdev_* functions guppyscreen calls (blank/unblank/brightness), an
// in-memory LVGL display of the AD5M's size, and a pointer input device
// that tests drive through ui::tap().

#include "lvgl/lvgl.h"

#include <atomic>

namespace fake_hw {

constexpr int WIDTH = 800;
constexpr int HEIGHT = 480;

// State recorded from the fbdev_* calls.
extern std::atomic<bool> blanked;
extern std::atomic<int> brightness;  // -1 until fbdev_brightness() is called

// Passed to GuppyScreen::init() in place of main.cpp's hal_init.
void hal_init(lv_color_t primary, lv_color_t secondary);

// Fake touch panel state, read by LVGL's input device.
void touch_set(int x, int y, bool pressed);

}  // namespace fake_hw

#endif  // GUPPY_TEST_FAKE_HW_H
