// An idle app runs no animations. LVGL's spinners animate forever from
// creation, hidden or not; the input-shaper and belts panels create three
// at startup (6 animations: ~15% of guppyscreen's idle CPU on the printer).
// Their animations run only while they are shown (KUtils::spinner_show/_hide).

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"

#include "guppyscreen.h"
#include "utils.h"

#include <mutex>

TEST_CASE("idle app runs no animations and spinners animate only while shown") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  app::pump(300);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK(lv_anim_count_running() == 0);

  lv_obj_t *s = lv_spinner_create(lv_layer_top(), 1000, 60);
  KUtils::spinner_hide(s);
  CHECK(lv_anim_count_running() == 0);
  CHECK(lv_obj_has_flag(s, LV_OBJ_FLAG_HIDDEN));

  KUtils::spinner_show(s);
  CHECK(lv_anim_count_running() == 2);
  CHECK_FALSE(lv_obj_has_flag(s, LV_OBJ_FLAG_HIDDEN));
  KUtils::spinner_show(s);  // again: no duplicates
  CHECK(lv_anim_count_running() == 2);

  KUtils::spinner_hide(s);
  CHECK(lv_anim_count_running() == 0);
  lv_obj_del(s);
}
