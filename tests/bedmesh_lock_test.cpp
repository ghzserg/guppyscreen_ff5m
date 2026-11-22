#include "doctest/doctest.h"
#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"
#include "guppyscreen.h"

#include <cstring>
#include <mutex>

TEST_CASE("bed mesh profiles cannot be loaded or removed during printing and pause") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["bed_mesh"]["profiles"]["test_mesh"] = json::object();
  moonraker.set_result("printer.objects.subscribe", subscription);
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::HOME);
  ui::tap_label("Bed Mesh");
  app::settle(moonraker);
  lv_obj_t *profiles = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(lv_scr_act(), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_table_class) && lv_table_get_col_cnt(obj) == 3 &&
          lv_table_get_row_cnt(obj) > 0 && lv_table_get_cell_value(obj, 0, 0) &&
          !strcmp(lv_table_get_cell_value(obj, 0, 0), "test_mesh"))
        profiles = obj;
    });
  }
  REQUIRE(profiles != nullptr);

  for (const char *state : {"printing", "paused", "complete"}) {
    moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", state}}}});
    app::settle(moonraker);
    bool locked = strcmp(state, "complete") != 0;
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      CHECK(lv_obj_has_state(profiles, LV_STATE_DISABLED) == locked);
      CHECK(lv_obj_has_flag(profiles, LV_OBJ_FLAG_CLICKABLE) == !locked);
      // Проверяем также обработчик, независимо от блокировки ввода LVGL.
      auto *table = reinterpret_cast<lv_table_t *>(profiles);
      table->row_act = 0;
      for (uint16_t col : {1, 2}) {
        table->col_act = col;
        lv_event_send(profiles, LV_EVENT_VALUE_CHANGED, nullptr);
      }
      table->row_act = table->col_act = LV_TABLE_CELL_NONE;
    }
    app::settle(moonraker);
    if (locked) {
      CHECK(moonraker.gcodes().empty());
    } else {
      auto codes = moonraker.gcodes();
      REQUIRE(codes.size() == 2);
      CHECK(codes[0] == "BED_MESH_PROFILE LOAD=\"test_mesh\"");
      CHECK(codes[1] == "BED_MESH_PROFILE REMOVE=\"test_mesh\"\nSAVE_CONFIG");
    }
  }
}
