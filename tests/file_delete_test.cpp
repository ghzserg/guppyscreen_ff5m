#include "doctest/doctest.h"
#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"
#include "guppyscreen.h"

namespace {

void open_file(FakeMoonraker &moonraker) {
  moonraker.set_result("server.files.list", json::array({
    {{"path", "models/test_file.gcode"}, {"modified", 123}}
  }));
  moonraker.set_result("server.files.metadata",
                       {{"modified", 123}, {"size", 1024}, {"thumbnails", json::array()}});
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_tab(Nav::PRINT);
  REQUIRE(app::wait_until([] { return ui::table_has_row("models"); }));
  ui::tap_table_row("models");
  REQUIRE(app::wait_until([] { return ui::table_has_row("test_file.gcode"); }));
  REQUIRE(ui::find_label("Delete") != nullptr);
}

void confirm(bool yes) {
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_shown(&lv_btnmatrix_class) != nullptr; }),
                  "confirmation did not open: " << ui::dump_screen());
  lv_obj_t *buttons = ui::find_shown(&lv_btnmatrix_class);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(std::string(lv_btnmatrix_get_btn_text(buttons, 0)) == "Cancel");
    CHECK(std::string(lv_btnmatrix_get_btn_text(buttons, 1)) == "OK");
  }
  lv_area_t area = ui::coords(buttons);
  ui::tap_at(area.x1 + lv_area_get_width(&area) * (yes ? 3 : 1) / 4,
             (area.y1 + area.y2) / 2);
}

}

TEST_CASE("file deletion requires confirmation") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open_file(moonraker);
  ui::tap_label("Delete");
  CHECK(moonraker.request_count("server.files.delete_file") == 0);
  confirm(false);
  CHECK(moonraker.request_count("server.files.delete_file") == 0);
}

TEST_CASE("file deletion sends gcodes rooted nested path") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open_file(moonraker);
  const int before = moonraker.request_count("server.files.list");
  moonraker.set_result("server.files.list", json::array());
  moonraker.set_result("server.files.delete_file", {{"action", "delete_file"}});
  ui::tap_label("Delete");
  confirm(true);
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.delete_file") == 1; }));
  auto requests = moonraker.requests("server.files.delete_file");
  REQUIRE(requests.size() == 1);
  CHECK(requests[0] == json({{"path", "gcodes/models/test_file.gcode"}}));
  CHECK(app::wait_until([&] {
    return moonraker.request_count("server.files.list") > before &&
           !ui::table_has_row("test_file.gcode");
  }));
}

TEST_CASE("file deletion error is visible and does not remove the row") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open_file(moonraker);
  moonraker.set_result("server.files.delete_file",
                       FakeMoonraker::error(403, "permission denied"));
  ui::tap_label("Delete");
  confirm(true);
  CHECK(app::wait_until([] { return ui::find_label("Could not delete the file.") != nullptr; }));
  CHECK(ui::table_has_row("test_file.gcode"));
}

TEST_CASE("file deletion is allowed during printing") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open_file(moonraker);
  ui::tap_label("Delete");
  moonraker.push_status({{"print_stats", {{"state", "printing"}}}});
  app::pump(100);
  confirm(true);
  CHECK(app::wait_until([&] {
    return moonraker.request_count("server.files.delete_file") == 1;
  }));
}
