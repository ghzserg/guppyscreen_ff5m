// The Print tab of the nav bar: the file list when idle, the print status
// screen while printing or paused.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "fake_printer.h"
#include "ui.h"

#include "guppyscreen.h"
#include "nav.h"
#include "state.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::vector<PrintFile> test_files() {
  PrintFile cube;
  cube.path = "cube.gcode";
  cube.estimated_time = 1500;
  cube.object_height = 10;
  cube.layer_height = 0.2;
  return {cube};
}

FakePrinter::Options fast(double print_seconds) {
  FakePrinter::Options o;
  o.print_seconds = print_seconds;
  o.heat_speed = 40;
  o.tick_ms = 50;
  return o;
}

std::string print_state() {
  return State::get_instance()
    ->get_data_copy("/printer_state/print_stats/state"_json_pointer)
    .get<std::string>();
}

Nav::Tab active_tab() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return Nav::get().active();
}

// The file list's table holds cube.gcode and a tap on it would reach it.
bool file_list_shown() {
  if (!ui::table_has_row("cube.gcode")) return false;
  lv_obj_t *table = ui::find_shown(&lv_table_class);
  return table != nullptr;
}
bool print_screen_shown() { return ui::find_label("Pause") != nullptr || ui::find_label("Resume") != nullptr; }

// Pick cube.gcode in the Print tab and press Print, as a user would.
void start_print(FakeMoonraker &moonraker) {
  ui::tap_tab(Nav::PRINT);
  REQUIRE(app::wait_until(file_list_shown));
  ui::tap_table_row("cube.gcode");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  ui::tap_label("Print");
  REQUIRE(app::wait_until([] { return print_state() == "printing"; }));
}

// Without FakePrinter: the print state comes only from what the test pushes.
void push_print(FakeMoonraker &moonraker, const json &print_stats) {
  moonraker.push_status({{"print_stats", print_stats}});
  app::pump(200);
}

void start_plain(FakeMoonraker &moonraker) {
  app::start(moonraker, {{"prompt_emergency_stop", false}});
  app::wait_ready(moonraker);
  push_print(moonraker, {{"filename", "cube.gcode"}, {"state", "printing"}});
  REQUIRE_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
}

}  // namespace

TEST_CASE("Print tab opens the file list when idle") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(1.5));
  app::start(moonraker);
  app::wait_ready(moonraker);

  CHECK(ui::is_shown(Nav::get().tab_button(Nav::PRINT)));
  CHECK_MESSAGE(ui::find_label("Print") == nullptr, "Home has no Print button: " << ui::dump_screen());
  ui::tap_tab(Nav::PRINT);
  CHECK_MESSAGE(app::wait_until(file_list_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
  CHECK(ui::find_label("Status") == nullptr);
}

TEST_CASE("a print start shows the print screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(30));
  app::start(moonraker);
  app::wait_ready(moonraker);

  start_print(moonraker);
  CHECK_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
  CHECK(!file_list_shown());
  CHECK(ui::find_label("Back") == nullptr);
  CHECK(ui::find_label("Stop") == nullptr);
}

TEST_CASE("Print tab returns to the file list when the print completes") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(1.5));
  app::start(moonraker);
  app::wait_ready(moonraker);

  start_print(moonraker);
  REQUIRE(app::wait_until([] { return print_state() == "complete"; }, 10000));
  CHECK_MESSAGE(app::wait_until(file_list_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
}

TEST_CASE("a completed print closes Fine Tune over the print screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter printer(moonraker, test_files(), fast(3));
  app::start(moonraker);
  app::wait_ready(moonraker);

  start_print(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  ui::tap_label("Fine Tune");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("Speed Reset") != nullptr; }), ui::dump_screen());
  REQUIRE(app::wait_until([] { return print_state() == "complete"; }, 10000));
  CHECK_MESSAGE(app::wait_until(file_list_shown), ui::dump_screen());
  CHECK(ui::find_label("Speed Reset") == nullptr);
}

TEST_CASE("pause and resume keep the open screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  ui::tap_tab(Nav::HOME);
  ui::tap_label("Homing");
  REQUIRE(ui::find_label("Motor off") != nullptr);
  push_print(moonraker, {{"state", "paused"}});
  push_print(moonraker, {{"state", "printing"}});
  CHECK_MESSAGE(ui::find_label("Motor off") != nullptr, ui::dump_screen());
  CHECK(active_tab() == Nav::HOME);
}

TEST_CASE("Cancel keeps the print screen until Klipper reports it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  ui::tap_label("Cancel");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.cancel") == 1; }));
  app::pump(200);
  CHECK_MESSAGE(print_screen_shown(), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);

  push_print(moonraker, {{"state", "cancelled"}});
  CHECK(!print_screen_shown());
  CHECK(active_tab() == Nav::PRINT);  // the file list now
}

TEST_CASE("empty filename does not activate the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  ui::tap_tab(Nav::TUNE);
  push_print(moonraker, {{"filename", ""}});
  CHECK(active_tab() == Nav::TUNE);
}

TEST_CASE("Fine Tune from the print screen is left by the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  ui::tap_label("Fine Tune");
  REQUIRE_MESSAGE(ui::find_label("Speed Reset") != nullptr, ui::dump_screen());
  ui::tap_tab(Nav::PRINT);
  CHECK(ui::find_label("Speed Reset") == nullptr);
  CHECK_MESSAGE(print_screen_shown(), ui::dump_screen());
}

TEST_CASE("connecting during a print activates the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["print_stats"]["state"] = "printing";
  sub["status"]["print_stats"]["filename"] = "cube.gcode";
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker);
  app::wait_ready(moonraker);

  CHECK_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);

  ui::tap_tab(Nav::HOME);
  moonraker.forget_requests();
  moonraker.drop_client();
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.objects.subscribe") > 0; }, 10000));
  app::settle(moonraker);
  CHECK_MESSAGE(app::wait_until(print_screen_shown), ui::dump_screen());
  CHECK(active_tab() == Nav::PRINT);
}

TEST_CASE("print screen fits the content area") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);

  // What one sees: labels and images. (The details grid's own box is wider
  // than its column and was clipped at 800 px already; its texts fit.)
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_obj_t *root = Nav::get().root(Nav::PRINT);
  int checked = 0;
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (!lv_obj_is_visible(o)) return;
    bool label = lv_obj_check_type(o, &lv_label_class);
    if (label || lv_obj_check_type(o, &lv_img_class)) {
      lv_area_t a;
      lv_obj_get_coords(o, &a);
      std::string what = label ? std::string("label ") + lv_label_get_text(o) : std::string("img");
      CHECK_MESSAGE(a.x1 >= 60, what << " x1 " << a.x1);
      CHECK_MESSAGE(a.x2 <= 799, what << " x2 " << a.x2);
      checked++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  walk(root);
  CHECK(checked > 10);
}

TEST_CASE("a print started elsewhere opens the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_plain(moonraker);
  push_print(moonraker, {{"state", "complete"}});
  REQUIRE(!print_screen_shown());

  // The same file again from a slicer or a macro: no filename change.
  ui::tap_tab(Nav::HOME);
  push_print(moonraker, {{"state", "printing"}});
  CHECK(active_tab() == Nav::PRINT);
  CHECK_MESSAGE(print_screen_shown(), ui::dump_screen());
}

TEST_CASE("a file selected without printing keeps the file list") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  push_print(moonraker, {{"filename", "cube.gcode"}});  // e.g. M23, state stays standby
  CHECK(active_tab() == Nav::HOME);
  ui::tap_tab(Nav::PRINT);
  CHECK_MESSAGE(!print_screen_shown(), ui::dump_screen());
}

TEST_CASE("a double tap on Print starts one print") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  FakePrinter::Options o = fast(30);
  o.tick_ms = 1000;  // Klipper is slow to report printing: the list stays up
  FakePrinter printer(moonraker, test_files(), o);
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_tab(Nav::PRINT);
  REQUIRE(app::wait_until(file_list_shown));
  ui::tap_table_row("cube.gcode");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  lv_obj_t *print = ui::find_label("Print");
  REQUIRE(print != nullptr);
  lv_area_t a;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_get_coords(lv_obj_get_parent(print), &a);
  }
  lv_coord_t x = (a.x1 + a.x2) / 2, y = (a.y1 + a.y2) / 2;
  ui::tap_at(x, y);
  ui::tap_at(x, y);
  app::pump(300);
  CHECK(moonraker.request_count("printer.print.start") == 1);
}

namespace {
// The Print tab button's background as drawn now: transparent or a colour.
bool print_tab_orange() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *btn = Nav::get().tab_button(Nav::PRINT);
  if (lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) != LV_OPA_COVER) return false;
  lv_color_t c = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
  return c.full == lv_palette_main(LV_PALETTE_ORANGE).full
    || c.full == lv_palette_darken(LV_PALETTE_ORANGE, 1).full;
}
}  // namespace

TEST_CASE("the Print tab is orange while printing") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"prompt_emergency_stop", false}});
  app::wait_ready(moonraker);
  CHECK(!print_tab_orange());

  push_print(moonraker, {{"filename", "cube.gcode"}, {"state", "printing"}});
  CHECK(print_tab_orange());  // the active tab
  ui::tap_tab(Nav::HOME);
  CHECK(print_tab_orange());  // and not the active one

  push_print(moonraker, {{"state", "paused"}});
  CHECK(!print_tab_orange());
  push_print(moonraker, {{"state", "printing"}});
  CHECK(print_tab_orange());
  push_print(moonraker, {{"state", "complete"}});
  CHECK(!print_tab_orange());
}

TEST_CASE("connecting during a print makes the Print tab orange") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["print_stats"]["state"] = "printing";
  sub["status"]["print_stats"]["filename"] = "cube.gcode";
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker);
  app::wait_ready(moonraker);
  REQUIRE(app::wait_until(print_screen_shown));
  CHECK(print_tab_orange());
}
