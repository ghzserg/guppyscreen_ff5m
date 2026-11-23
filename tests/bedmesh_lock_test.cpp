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
      if (!lv_obj_check_type(obj, &lv_table_class) || lv_table_get_col_cnt(obj) != 3) return;
      for (uint16_t row = 0; row < lv_table_get_row_cnt(obj); row++) {
        const char *name = lv_table_get_cell_value(obj, row, 0);
        if (name && !strcmp(name, "test_mesh")) {
          profiles = obj;
          break;
        }
      }
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
      uint16_t target_row = LV_TABLE_CELL_NONE;
      for (uint16_t row = 0; row < lv_table_get_row_cnt(profiles); row++) {
        if (!strcmp(lv_table_get_cell_value(profiles, row, 0), "test_mesh")) {
          target_row = row;
          break;
        }
      }
      REQUIRE(target_row != LV_TABLE_CELL_NONE);
      table->row_act = target_row;
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


TEST_CASE("BED_MESH is displayed by default in the test fixture") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::HOME);
  ui::tap_label("Bed Mesh");
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());

  bool profile_found = false;
  bool map_found = false;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(lv_scr_act(), [&](lv_obj_t *obj) {
      if (!lv_obj_check_type(obj, &lv_table_class) ||
          lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
      if (lv_table_get_col_cnt(obj) == 3) {
        for (uint16_t row = 0; row < lv_table_get_row_cnt(obj); row++) {
          if (!strcmp(lv_table_get_cell_value(obj, row, 0), "BED_MESH")) profile_found = true;
        }
      } else if (lv_table_get_col_cnt(obj) == 5 && lv_table_get_row_cnt(obj) == 5) {
        map_found = true;
      }
    });
  }
  CHECK(profile_found);
  CHECK(map_found);
}

TEST_CASE("startup loads saved BED_MESH when no mesh is active") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["bed_mesh"]["profile_name"] = "";
  subscription["status"]["bed_mesh"]["probed_matrix"] = json::array({json::array()});
  subscription["status"]["bed_mesh"]["mesh_matrix"] = json::array({json::array()});
  moonraker.set_result("printer.objects.subscribe", subscription);

  app::start(moonraker);
  app::wait_ready(moonraker);
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes() == std::vector<std::string>{"BED_MESH_PROFILE LOAD=\"BED_MESH\""});
}

TEST_CASE("startup does not replace a different active bed mesh") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["bed_mesh"]["profile_name"] = "custom";
  subscription["status"]["bed_mesh"]["profiles"]["custom"] = subscription["status"]["bed_mesh"]["profiles"]["BED_MESH"];
  moonraker.set_result("printer.objects.subscribe", subscription);

  app::start(moonraker);
  app::wait_ready(moonraker);
  CHECK(moonraker.gcodes().empty());
}

TEST_CASE("startup does not load BED_MESH if it is not saved") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["bed_mesh"]["profile_name"] = "";
  subscription["status"]["bed_mesh"]["profiles"].erase("BED_MESH");
  moonraker.set_result("printer.objects.subscribe", subscription);

  app::start(moonraker);
  app::wait_ready(moonraker);
  CHECK(moonraker.gcodes().empty());
}

TEST_CASE("startup does not change a bed mesh during printing") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["bed_mesh"]["profile_name"] = "";
  subscription["status"]["print_stats"]["state"] = "printing";
  subscription["status"]["print_stats"]["filename"] = "cube.gcode";
  moonraker.set_result("printer.objects.subscribe", subscription);

  app::start(moonraker);
  app::wait_ready(moonraker);
  CHECK(moonraker.gcodes().empty());
}

TEST_CASE("startup without bed_mesh support does not load a profile") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"].erase("bed_mesh");
  moonraker.set_result("printer.objects.subscribe", subscription);

  app::start(moonraker);
  app::wait_ready(moonraker);
  CHECK(moonraker.gcodes().empty());
}
