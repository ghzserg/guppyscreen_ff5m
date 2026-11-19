// Home's button icons (FF5M):
// - Fine Tune on Home shows the icon of Fine Tune's Speed+ button, on the
//   print status screen a speedometer, so neither looks like the Tune tab's
//   sliders in the nav bar (the Tune panel has no Fine Tune button on the
//   FF5M);
// - Extrude on Home shows the icon of the extruder screen's Load button.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>

LV_IMG_DECLARE(speed_img);
LV_IMG_DECLARE(speed_up_img);
LV_IMG_DECLARE(fine_tune_img);
LV_IMG_DECLARE(extrude_img);

namespace {

// The image of every button labelled `text`, shown or not.
std::vector<const void *> button_images(const char *text) {
  std::vector<const void *> found;
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) {
      lv_obj_t *parent = lv_obj_get_parent(o);
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
        lv_obj_t *c = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(c, &lv_imgbtn_class)) found.push_back(lv_imgbtn_get_src_middle(c, LV_IMGBTN_STATE_RELEASED));
      }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  walk(lv_scr_act());
  return found;
}

}  // namespace

TEST_CASE("Fine Tune on Home shows the Speed+ icon and on the print screen a speedometer") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  auto fine_tune = button_images("Fine Tune");
  auto speed_up = button_images("Speed+");
  REQUIRE(speed_up.size() == 1);
  CHECK(speed_up[0] == &speed_up_img);
  REQUIRE(fine_tune.size() == 2);
  // The one shown on Home; the other is on the print status screen.
  lv_obj_t *home_label = ui::find_label("Fine Tune");
  REQUIRE(home_label != nullptr);
  const void *home = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *parent = lv_obj_get_parent(home_label);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
      lv_obj_t *c = lv_obj_get_child(parent, i);
      if (lv_obj_check_type(c, &lv_imgbtn_class)) home = lv_imgbtn_get_src_middle(c, LV_IMGBTN_STATE_RELEASED);
    }
  }
  CHECK(home == &speed_up_img);
  CHECK(std::count(fine_tune.begin(), fine_tune.end(), &speed_img) == 1);
}

TEST_CASE("Extrude on Home shows the icon of the Load button") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  auto extrude = button_images("Extrude");
  auto load = button_images("Load");
  REQUIRE(extrude.size() == 1);
  REQUIRE(load.size() == 1);
  CHECK(load[0] == &extrude_img);
  CHECK(extrude[0] == load[0]);
}
