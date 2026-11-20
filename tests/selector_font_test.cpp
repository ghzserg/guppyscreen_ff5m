// The number buttons of Homing, Extrude and Fine Tune use the sensor tiles'
// digit font; Fine Tune's Z/PA step buttons are as wide as the multiplier's
// and drop the leading zero (.005); Fine Tune's values use the same font.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"
#include "sensor_container.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

// The first button matrix whose button `idx` reads `text`.
lv_obj_t *buttons(const char *text, uint32_t idx = 0) {
  auto found = ui::btnmatrices(text, idx);
  REQUIRE_MESSAGE(!found.empty(), "no buttons with " << std::string(text));
  return found.front();
}

// The selector (buttons and title) whose fourth button reads `fourth`.
lv_obj_t *selector_with(const char *fourth) {
  lv_obj_t *m = buttons(fourth, 3);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_obj_get_parent(m);
}

lv_area_t coords(lv_obj_t *o) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  return a;
}

void open_fine_tune(FakeMoonraker &moonraker) {
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_label("Fine Tune");
  app::settle(moonraker);
}

const lv_font_t *font(lv_obj_t *btnm) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_obj_get_style_text_font(btnm, LV_PART_ITEMS);
}

}  // namespace

TEST_CASE("number buttons of Homing Extrude and Fine Tune use the tiles' digit font") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  // Homing distance; Extrude temperature, length and speed; Fine Tune steps.
  for (const char *first : {".1", "200", "10", "1", ".005"}) {
    INFO(std::string(first));
    CHECK(font(buttons(first)) == SensorContainer::temperature_font());
  }
  // The multiplier's "1" and the speed's "1": check both matrices.
  auto ones = ui::btnmatrices("1");
  CHECK(ones.size() == 2);
  for (lv_obj_t *o : ones) CHECK(font(o) == SensorContainer::temperature_font());
}

TEST_CASE("Fine Tune's Z and PA step buttons are as wide as the multiplier's") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  lv_obj_t *z = selector_with(".10"), *m = selector_with("25");  // the multiplier: 1, 5, 10, 25
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  INFO("Z/PA " << lv_obj_get_width(z) << ", multiplier " << lv_obj_get_width(m));
  CHECK(lv_obj_get_width(z) == lv_obj_get_width(m));
}

TEST_CASE("Fine Tune's values use the number buttons' font") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_label("Fine Tune");
  app::settle(moonraker);

  for (const char *text : {"100%", "mm/s", "mm"}) {
    lv_obj_t *label = ui::find_label(text, text[0] != '1' ? ui::Match::Contains : ui::Match::Exact);
    REQUIRE_MESSAGE(label != nullptr, text << " not shown: " << ui::dump_screen());
    INFO(std::string(text));
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_get_style_text_font(label, 0) == SensorContainer::temperature_font());
  }
}

// Fine Tune: the step buttons on top, the four columns of buttons under
// them, the values in one row at the bottom, each under its column.
TEST_CASE("Fine Tune has its step buttons on top") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open_fine_tune(moonraker);
  lv_obj_t *reset = ui::find_label("Z Reset");
  REQUIRE(reset != nullptr);
  lv_obj_t *z = selector_with(".10"), *m = selector_with("25");
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  int reset_top = coords(lv_obj_get_parent(reset)).y1;
  INFO("Z/PA bottom " << coords(z).y2 << ", multiplier bottom " << coords(m).y2 << ", Reset Z top " << reset_top);
  CHECK(coords(z).y2 < reset_top);
  CHECK(coords(m).y2 < reset_top);
}

TEST_CASE("Fine Tune shows its values in the bottom row without icons") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open_fine_tune(moonraker);
  // Each value under the buttons of its column.
  const std::pair<const char *, const char *> columns[] = {
    {"0.000 mm", "Z-"}, {"0.025 mm/s", "PA-"}, {"100%", "Speed-"}, {"100%", "Flow-"}};
  std::vector<lv_obj_t *> hundreds;
  for (auto [text, down] : columns) {
    lv_obj_t *button = ui::find_label(down);
    REQUIRE_MESSAGE(button != nullptr, std::string(down) << " not shown");
    lv_obj_t *label = nullptr;
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      lv_obj_update_layout(lv_scr_act());
      lv_area_t b = coords(lv_obj_get_parent(button));
      int centre = (b.x1 + b.x2) / 2;
      // The value label shown below the button, across its centre.
      ui::walk(lv_scr_act(), [&](lv_obj_t *o) {
        if (!label && lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) {
          lv_area_t l = coords(o);
          if (l.x1 <= centre && centre <= l.x2) label = o;
        }
      });
      REQUIRE_MESSAGE(label != nullptr, std::string(text) << " not under " << down);
      lv_area_t l = coords(label);
      INFO(std::string(text) << ": label y " << l.y1 << ".." << l.y2 << ", " << down << " bottom " << b.y2);
      CHECK(l.y1 > b.y2);
      // No icon shown beside it.
      lv_obj_t *box = lv_obj_get_parent(label);
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(box); i++) {
        lv_obj_t *c = lv_obj_get_child(box, i);
        if (lv_obj_check_type(c, &lv_img_class)) CHECK(lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN));
      }
    }
    CHECK(ui::is_shown(label));
  }
}

// Each step selector centred over the two columns it serves.
TEST_CASE("Fine Tune's step buttons are centred over their columns") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open_fine_tune(moonraker);
  const struct { const char *fourth, *left, *right; } rows[] = {
    {".10", "Z Reset", "PA Reset"}, {"25", "Speed Reset", "Flow Reset"}};
  for (auto r : rows) {
    lv_obj_t *left = ui::find_label(r.left), *right = ui::find_label(r.right);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    lv_obj_t *m = selector_with(r.fourth);
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_update_layout(lv_scr_act());
    lv_area_t la = coords(lv_obj_get_parent(left)), ra = coords(lv_obj_get_parent(right)), ma = coords(m);
    int cell_centre = (la.x1 + la.x2 + ra.x1 + ra.x2) / 4;
    INFO(std::string(r.fourth) << ": buttons " << ma.x1 << ".." << ma.x2 << ", columns' centre " << cell_centre);
    CHECK(std::abs((ma.x1 + ma.x2) / 2 - cell_centre) <= 1);
  }
}
