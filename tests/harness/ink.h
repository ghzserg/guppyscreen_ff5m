#ifndef GUPPY_TEST_INK_H
#define GUPPY_TEST_INK_H

// Text measured by its pixels on the fake display (fake_hw::pixel): where
// it is drawn, not where its box is. Call after app::redraw().

#include "lvgl/lvgl.h"

#include <functional>
#include <string>

namespace ink {

struct Rows {
  int top = -1, bottom = -1;  // -1: no ink
};

// The first and last row of `area` with a pixel in columns x1..x2 unlike
// background(x, y).
Rows rows(const lv_area_t &area, lv_coord_t x1, lv_coord_t x2,
          const std::function<lv_color_t(int x, int y)> &background);

// The digits of a text in SensorContainer::temperature_font() are centred in
// `box` (a frame's or a button's inside), within half a pixel: as much room
// above them as under them. By the digits, not the whole ink: commas hang
// under the baseline. Looks at columns x1..x2; the background is the box's
// top row. Hold the LVGL lock.
void check_digits_centred(const std::string &what, const lv_area_t &box, lv_coord_t x1, lv_coord_t x2);

// The same for a value label in its frame (an ImageLabel's bordered box).
void check_framed(lv_obj_t *label);

}  // namespace ink

#endif  // GUPPY_TEST_INK_H
