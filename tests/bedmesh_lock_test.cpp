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

TEST_CASE("bed mesh profile names do not overlap the mesh grid") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Bed Mesh");
  app::settle(moonraker);

  lv_obj_t *grid = nullptr;
  lv_obj_t *profiles = nullptr;
  lv_area_t grid_area{}, profiles_area{};
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_update_layout(lv_scr_act());
    ui::walk(lv_scr_act(), [&](lv_obj_t *obj) {
      if (!lv_obj_check_type(obj, &lv_table_class) ||
          lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
      if (lv_table_get_col_cnt(obj) == 5 && lv_table_get_row_cnt(obj) == 5) grid = obj;
      if (lv_table_get_col_cnt(obj) == 3 && lv_table_get_row_cnt(obj) > 0 &&
          !strcmp(lv_table_get_cell_value(obj, 0, 0), "BED_MESH")) profiles = obj;
    });
    if (grid) lv_obj_get_coords(grid, &grid_area);
    if (profiles) lv_obj_get_coords(profiles, &profiles_area);
  }
  REQUIRE(grid != nullptr);
  REQUIRE(profiles != nullptr);
  CHECK(grid_area.x2 + 1 < profiles_area.x1);
}

TEST_CASE("startup never loads a saved mesh automatically") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["bed_mesh"]["profile_name"] = "";
  moonraker.set_result("printer.objects.subscribe", subscription);

  app::start(moonraker);
  app::wait_ready(moonraker);
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());
}

TEST_CASE("screw adjustment shows fresh Klipper results and can repeat the measurement") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["configfile"]["settings"]["screws_tilt_adjust"] = {
    {"screw1", {12.5, 12.5}}, {"screw1_name", "Left Near"},
    {"screw2", {202, 12.5}}, {"screw2_name", "Right Near"},
    {"screw3", {202, 202}}, {"screw3_name", "Right Far"},
    {"screw4", {12.5, 202}}, {"screw4_name", "Left Far"}
  };
  subscription["status"]["screws_tilt_adjust"]["results"] = {
    {"screw1", {{"z", -1.8675}, {"is_base", true}, {"sign", "CW"}, {"adjust", "00:00"}}},
    {"screw2", {{"z", -1.8692}, {"is_base", false}, {"sign", "CW"}, {"adjust", "00:00"}}},
    {"screw3", {{"z", -1.9092}, {"is_base", false}, {"sign", "CW"}, {"adjust", "00:05"}}},
    {"screw4", {{"z", -1.9175}, {"is_base", false}, {"sign", "CCW"}, {"adjust", "00:06"}}}
  };
  moonraker.set_result("printer.objects.subscribe", subscription);
  app::start(moonraker, {{"screw_nozzle_temp", 160}, {"screw_bed_temp", 55},
                        {"prompt_emergency_stop", false}});
  app::wait_ready(moonraker);
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Bed Mesh");

  moonraker.hold_next_reply("printer.gcode.script");
  ui::tap_label("Screw adjustment");
  app::settle(moonraker);
  CHECK(moonraker.gcodes() == std::vector<std::string>{
    "BED_LEVEL_SCREWS_TUNE EXTRUDER_TEMP=160 BED_TEMP=55"
  });
  CHECK(ui::find_label("Measuring bed screws…"));
  CHECK(ui::is_shown(Nav::get().tab_button(Nav::HOME)));
  CHECK(ui::is_shown(Nav::get().stop_button()));
  ui::tap_tab(Nav::TUNE);
  CHECK(Nav::get().active() == Nav::HOME);
  CHECK(ui::find_label("Measuring bed screws…"));
  CHECK_FALSE(ui::table_has_row("Left Near"));
  const auto queries_before = moonraker.request_count("printer.objects.query");
  moonraker.release_reply();
  REQUIRE(app::wait_until([] { return ui::table_has_row("Left Near"); }));
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *results = nullptr;
    ui::walk(lv_scr_act(), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_table_class) && lv_table_get_col_cnt(obj) == 3 &&
          lv_table_get_row_cnt(obj) == 5 &&
          !strcmp(lv_table_get_cell_value(obj, 0, 2), "Adjustment")) {
        results = obj;
      }
    });
    REQUIRE(results != nullptr);
    lv_obj_t *dialog = lv_obj_get_parent(results);
    lv_obj_update_layout(dialog);
    lv_area_t bounds{};
    lv_obj_get_coords(dialog, &bounds);
    lv_area_t content_bounds{};
    lv_obj_get_coords(Nav::get().content(), &content_bounds);
    CHECK(bounds.x1 == content_bounds.x1);
    CHECK(bounds.y1 == content_bounds.y1);
    CHECK(bounds.x2 == content_bounds.x2);
    CHECK(bounds.y2 == content_bounds.y2);
    CHECK(std::string(lv_table_get_cell_value(results, 1, 0)) == "Left Near\nX 12.5   Y 12.5");
    CHECK(std::string(lv_table_get_cell_value(results, 1, 1)) == "-1.8675");
    CHECK(std::string(lv_table_get_cell_value(results, 1, 2)) == "Reference");
    CHECK(std::string(lv_table_get_cell_value(results, 2, 2)) == "No turn\n00:00");
    CHECK(std::string(lv_table_get_cell_value(results, 3, 2)) == "Clockwise\n00:05");
    CHECK(std::string(lv_table_get_cell_value(results, 4, 2)) == "Counterclockwise\n00:06");
    CHECK_FALSE(lv_table_has_cell_ctrl(results, 1, 2, LV_TABLE_CELL_CTRL_CUSTOM_1));
    CHECK_FALSE(lv_table_has_cell_ctrl(results, 1, 2, LV_TABLE_CELL_CTRL_CUSTOM_2));
    CHECK(lv_table_has_cell_ctrl(results, 2, 2, LV_TABLE_CELL_CTRL_CUSTOM_1));
    CHECK(lv_table_has_cell_ctrl(results, 3, 2, LV_TABLE_CELL_CTRL_CUSTOM_1));
    CHECK_FALSE(lv_table_has_cell_ctrl(results, 3, 2, LV_TABLE_CELL_CTRL_CUSTOM_2));
    CHECK_FALSE(lv_table_has_cell_ctrl(results, 4, 2, LV_TABLE_CELL_CTRL_CUSTOM_1));
    CHECK(lv_table_has_cell_ctrl(results, 4, 2, LV_TABLE_CELL_CTRL_CUSTOM_2));
  }
  CHECK(moonraker.request_count("printer.objects.query") == queries_before + 1);

  ui::tap_label("Measure again");
  REQUIRE(app::wait_until([] { return ui::table_has_row("Left Far"); }));
  auto codes = moonraker.gcodes();
  REQUIRE(codes.size() == 2);
  CHECK(codes[0] == codes[1]);
  CHECK(moonraker.request_count("printer.objects.query") == queries_before + 2);

  moonraker.set_handler("printer.gcode.script", [](const json &) {
    return FakeMoonraker::error(400, "Probe failed");
  });
  ui::tap_label("Measure again");
  REQUIRE(app::wait_until([] { return ui::find_label("Probe failed", ui::Match::Contains); }));
  CHECK_FALSE(ui::table_has_row("Left Near"));
  CHECK(moonraker.request_count("printer.objects.query") == queries_before + 2);
  ui::tap_label("Close");
  CHECK_FALSE(ui::find_label("Probe failed", ui::Match::Contains));
  CHECK(ui::is_shown(Nav::get().tab_button(Nav::HOME)));

  moonraker.hold_next_reply("printer.gcode.script");
  ui::tap_label("Screw adjustment");
  REQUIRE(app::wait_until([] { return ui::find_label("Measuring bed screws…"); }));
  moonraker.push_status({{"print_stats", {{"state", "paused"}}}});
  app::settle(moonraker);
  moonraker.release_reply();
  app::settle(moonraker);
  CHECK_FALSE(ui::find_label("Measuring bed screws…"));
  CHECK_FALSE(ui::find_label("Measure again"));
  CHECK(moonraker.request_count("printer.objects.query") == queries_before + 2);

  moonraker.push_status({{"print_stats", {{"state", "complete"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Bed Mesh");
  ui::tap_label("Screw adjustment");
  REQUIRE(app::wait_until([] { return ui::find_label("Probe failed", ui::Match::Contains); }));

  CHECK(ui::is_shown(Nav::get().stop_button()));
  ui::tap(Nav::get().stop_button());
  CHECK(app::wait_until([&] { return moonraker.request_count("printer.emergency_stop") == 1; }));
}
