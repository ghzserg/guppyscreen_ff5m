#include "ink.h"

#include "doctest/doctest.h"
#include "fake_hw.h"

#include "sensor_container.h"

#include <cstdlib>

namespace ink {

Rows rows(const lv_area_t &area, lv_coord_t x1, lv_coord_t x2,
          const std::function<lv_color_t(int x, int y)> &background) {
  Rows r;
  for (int y = area.y1; y <= area.y2; y++) {
    for (int x = x1; x <= x2; x++) {
      if (fake_hw::pixel(x, y).full != background(x, y).full) {
        if (r.top < 0) r.top = y;
        r.bottom = y;
        break;
      }
    }
  }
  return r;
}

void check_digits_centred(const std::string &what, const lv_area_t &box, lv_coord_t x1, lv_coord_t x2) {
  lv_font_glyph_dsc_t zero;
  lv_font_get_glyph_dsc(SensorContainer::temperature_font(), &zero, '0', 0);
  Rows i = rows(box, x1, x2, [&](int x, int) { return fake_hw::pixel(x, box.y1); });
  INFO(what << ": ink from " << i.top << ", digits " << zero.box_h << " px, box " << box.y1 << ".." << box.y2);
  REQUIRE(i.top >= 0);
  int digits_bottom = i.top + zero.box_h - 1;
  CHECK(std::abs((i.top - box.y1) - (box.y2 - digits_bottom)) <= 1);
}

void check_framed(lv_obj_t *label) {
  lv_obj_t *frame = lv_obj_get_parent(label);
  lv_area_t f, l;
  lv_obj_get_coords(frame, &f);
  lv_obj_get_coords(label, &l);
  lv_coord_t bw = lv_obj_get_style_border_width(frame, 0);
  lv_area_t inside = {(lv_coord_t)(f.x1 + bw), (lv_coord_t)(f.y1 + bw), (lv_coord_t)(f.x2 - bw), (lv_coord_t)(f.y2 - bw)};
  check_digits_centred(lv_label_get_text(label), inside, l.x1, l.x2);
}

}  // namespace ink
