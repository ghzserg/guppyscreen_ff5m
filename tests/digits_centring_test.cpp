// The digits font (SensorContainer::temperature_font) keeps room for
// descenders under its baseline. Texts centred by their box must still look
// centred by their ink: the values in frames in Fine Tune and the numbers on
// the step buttons of Fine Tune, Extrude and Homing (the print screen's are
// in print_tab_test.cpp).

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ink.h"
#include "ui.h"

#include "guppyscreen.h"
#include "nav.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

// Every button of a button matrix whose button `idx` reads `text`.
void check_buttons(const char *text, uint32_t idx = 0) {
  lv_obj_t *btnm = nullptr;
  for (lv_obj_t *m : ui::btnmatrices(text, idx)) {
    if (ui::is_shown(m)) {
      btnm = m;
      break;
    }
  }
  REQUIRE_MESSAGE(btnm != nullptr, "no buttons shown with " << std::string(text));
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_btnmatrix_t *m = (lv_btnmatrix_t *)btnm;
  lv_area_t c;
  lv_obj_get_coords(btnm, &c);
  lv_coord_t bw = lv_obj_get_style_border_width(btnm, LV_PART_ITEMS);
  for (uint16_t i = 0; i < m->btn_cnt; i++) {
    const char *t = lv_btnmatrix_get_btn_text(btnm, i);
    if (t == nullptr || t[0] == 0 || strcmp(t, "\n") == 0) continue;
    lv_area_t a = m->button_areas[i];
    lv_area_t box = {c.x1 + a.x1 + bw, c.y1 + a.y1 + bw, c.x1 + a.x2 - bw, c.y1 + a.y2 - bw};
    // The middle half: the centred text, clear of the rounded corners.
    lv_coord_t w = lv_area_get_width(&box);
    ink::check_digits_centred(t, box, box.x1 + w / 4, box.x2 - w / 4);
  }
}

}  // namespace

TEST_CASE("Fine Tune's values and step buttons are centred") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_label("Fine Tune");
  app::settle(moonraker);
  REQUIRE(ui::find_label("100%") != nullptr);

  app::redraw();
  check_buttons(".005");
  check_buttons("1");
  lv_obj_t *percent = ui::find_label("100%");
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *values = lv_obj_get_parent(lv_obj_get_parent(percent));
  CHECK(lv_obj_get_child_cnt(values) == 4);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(values); i++) {
    lv_obj_t *frame = lv_obj_get_child(values, i);
    for (uint32_t j = 0; j < lv_obj_get_child_cnt(frame); j++) {
      lv_obj_t *label = lv_obj_get_child(frame, j);
      if (lv_obj_check_type(label, &lv_label_class)) ink::check_framed(label);
    }
  }
}

TEST_CASE("Extrude's and Homing's number buttons are centred") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_label("Extrude");
  app::settle(moonraker);
  app::redraw();
  for (const char *first : {"200", "10", "1"}) {
    INFO(std::string(first));
    check_buttons(first);
  }

  ui::tap_tab(Nav::HOME);
  ui::tap_label("Homing");
  app::settle(moonraker);
  app::redraw();
  check_buttons(".1");
}
